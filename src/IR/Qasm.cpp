// Spec 14 §11 — OpenQASM 3 emission. The text re-parses with lang::parseProgram and rebuilds to the
// same dump: declarations follow the flat bit order, modifiers are spelled out, numbers print in
// shortest round-trip form, and calibrations come back from the circuit metadata.
#include "IR/Circuit.hpp"
#include "IR/Emit.hpp"
#include "Lang/StdGates.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::ir {
namespace {

class Emitter {
public:
    explicit Emitter(const Circuit& c) : c_(c) {}

    Result<std::string> run() {
        QXL_TRY(nameWires());
        QXL_TRY(nameBits());
        std::string out = "OPENQASM 3.0;\ninclude \"stdgates.inc\";\n";
        const auto& meta = c_.meta();
        const bool calibrated = meta.contains("calibrations") && meta["calibrations"].is_array() && !meta["calibrations"].empty();
        if (calibrated) out += "defcalgrammar \"openpulse\";\n";
        if (meta.contains("device") && meta["device"].is_string()) out += std::format("pragma qlab.device {}\n", meta["device"].get<std::string>());
        if (meta.contains("shots") && meta["shots"].is_number_unsigned()) out += std::format("pragma qlab.shots {}\n", meta["shots"].get<std::uint64_t>());
        if (meta.contains("seed") && meta["seed"].is_number_unsigned()) out += std::format("pragma qlab.seed {}\n", meta["seed"].get<std::uint64_t>());
        if (meta.contains("optimize") && meta["optimize"].is_number_integer()) out += std::format("pragma qlab.optimize {}\n", meta["optimize"].get<int>());
        if (c_.isPhysical()) out += "pragma qlab.layout physical\n";
        if (meta.contains("pulse_level") && meta["pulse_level"].is_boolean()) out += std::format("pragma qlab.pulse_level {}\n", meta["pulse_level"].get<bool>() ? "on" : "off");
        out += declarations_;
        if (calibrated)
            for (const auto& cal : meta["calibrations"]) if (cal.is_string()) out += cal.get<std::string>() + "\n";
        QXL_TRY(body(c_, 0, out));
        return out;
    }

private:
    Status nameWires() {
        const std::uint32_t n = c_.qubitCount();
        wire_.assign(n, {});
        if (c_.isPhysical()) {
            for (std::uint32_t q = 0; q < n; ++q) wire_[q] = std::format("${}", q);
            return {};
        }
        if (c_.qubitRegisters().empty() && n > 0) {   // a register-less circuit built through the API
            const std::string name = freeName("q");
            declarations_ += std::format("qubit[{}] {};\n", n, name);
            for (std::uint32_t q = 0; q < n; ++q) wire_[q] = std::format("{}[{}]", name, q);
            return {};
        }
        for (const auto& r : c_.qubitRegisters()) {
            declarations_ += r.scalar ? std::format("qubit {};\n", r.name) : std::format("qubit[{}] {};\n", r.size, r.name);
            for (std::uint32_t i = 0; i < r.size && r.first + i < n; ++i)
                wire_[r.first + i] = r.scalar ? r.name : std::format("{}[{}]", r.name, i);
        }
        for (std::uint32_t q = 0; q < n; ++q)
            if (wire_[q].empty()) return fail(err::Unsupported, std::format("toQasm: wire {} belongs to no qubit register", q));
        return {};
    }

    Status nameBits() {
        const std::uint32_t n = c_.clbitCount();
        bit_.assign(n, {});
        regs_.clear();
        for (const auto& r : c_.bitRegisters()) regs_.push_back(&r);
        if (regs_.empty() && n > 0) {
            synthetic_ = BitRegister{freeName("c"), 0, n, RegKind::Bit, false};
            regs_.push_back(&*synthetic_);
        }
        // Declaration order = flat bit order, so a rebuild allocates the same bits (spec 14 §3).
        std::sort(regs_.begin(), regs_.end(), [](const BitRegister* a, const BitRegister* b) { return a->first < b->first; });
        for (const BitRegister* r : regs_) {
            // `bit c;` is one bit, `int k;` is int[32] (spec 13 §3 default widths), `bool` has none.
            std::string type;
            switch (r->kind) {
            case RegKind::Bit: type = r->scalar ? "bit" : std::format("bit[{}]", r->size); break;
            case RegKind::Bool: type = "bool"; break;
            case RegKind::Int: case RegKind::Uint:
                type = std::format("{}{}", r->kind == RegKind::Int ? "int" : "uint",
                                   r->scalar && r->size == 32 ? std::string() : std::format("[{}]", r->size));
                break;
            }
            declarations_ += std::format("{} {};\n", type, r->name);
            for (std::uint32_t i = 0; i < r->size && r->first + i < n; ++i)
                bit_[r->first + i] = r->scalar ? r->name : std::format("{}[{}]", r->name, i);
        }
        for (std::uint32_t b = 0; b < n; ++b)
            if (bit_[b].empty()) return fail(err::Unsupported, std::format("toQasm: bit {} belongs to no register", b));
        return {};
    }

    std::string freeName(std::string base) const {
        auto used = [&](const std::string& s) {
            return std::any_of(c_.bitRegisters().begin(), c_.bitRegisters().end(), [&](const auto& r) { return r.name == s; }) ||
                   std::any_of(c_.qubitRegisters().begin(), c_.qubitRegisters().end(), [&](const auto& r) { return r.name == s; });
        };
        while (used(base)) base += "_";
        return base;
    }

    Result<std::string> wire(Wire w) const {
        if (w.index >= wire_.size()) return fail(err::BadWire, std::format("toQasm: wire {} is outside the circuit", w.index));
        return wire_[w.index];
    }
    Result<std::string> bit(ClassicalBit b) const {
        if (b.index >= bit_.size()) return fail(err::BadBit, std::format("toQasm: bit {} is outside the circuit", b.index));
        return bit_[b.index];
    }
    Result<std::string> wires(const std::vector<Wire>& ws) const {
        std::string s;
        for (std::size_t i = 0; i < ws.size(); ++i) {
            QXL_TRY_ASSIGN(std::string t, wire(ws[i]));
            s += (i ? ", " : "") + t;
        }
        return s;
    }
    // A span of bits as a register name or an inclusive slice of one register.
    Result<std::string> span(CregRef r) const {
        for (const BitRegister* reg : regs_) {
            if (r.first < reg->first || r.first >= reg->first + reg->size) continue;
            if (r.first + r.size > reg->first + reg->size) break;
            if (r.first == reg->first && r.size == reg->size) return reg->name;
            if (reg->scalar) break;
            const std::uint32_t a = r.first - reg->first;
            return std::format("{}[{}:{}]", reg->name, a, a + r.size - 1);
        }
        return fail(err::Unsupported, std::format("toQasm: bits {}..{} do not lie inside one register", r.first, r.first + r.size - 1));
    }
    Result<std::string> expr(const ClassicalExpr& e) const {
        switch (e.op) {
        case ClassOp::Const: return std::format("{}", e.value);
        case ClassOp::BitRef: return bit(e.bit);
        case ClassOp::RegRef: return span(e.reg);
        case ClassOp::LogicNot: case ClassOp::BitNot: case ClassOp::Neg: {
            if (e.args.size() != 1) return fail(err::BadNode, "toQasm: malformed unary classical expression");
            QXL_TRY_ASSIGN(std::string a, expr(e.args[0]));
            return std::format("{}({})", classOpName(e.op), a);
        }
        default: {
            if (e.args.size() != 2) return fail(err::BadNode, "toQasm: malformed binary classical expression");
            QXL_TRY_ASSIGN(std::string a, expr(e.args[0]));
            QXL_TRY_ASSIGN(std::string b, expr(e.args[1]));
            return std::format("({} {} {})", a, classOpName(e.op), b);
        }
        }
    }
    // A whole condition or right-hand side: every sub-expression stays parenthesised, the outermost
    // pair is dropped (parentheses create no AST node, so the rebuild is unchanged).
    Result<std::string> topExpr(const ClassicalExpr& e) const {
        QXL_TRY_ASSIGN(std::string s, expr(e));
        const bool binary = e.args.size() == 2 && e.op != ClassOp::Const && e.op != ClassOp::BitRef && e.op != ClassOp::RegRef;
        return binary ? s.substr(1, s.size() - 2) : s;
    }

    Result<std::string> gate(const Gate& g) const {
        if (g.custom) return fail(err::Unsupported, "toQasm: a gate defined by an explicit matrix has no OpenQASM 3 spelling");
        std::string name = g.name;
        bool inverted = g.adjoint;
        if (name == "sxdg") { name = "sx"; inverted = !inverted; }   // not in stdgates.inc
        if (!g.opaque && !lang::StdGates::find(name))
            return fail(err::Unsupported, std::format("toQasm: gate '{}' is not in stdgates.inc", g.name));
        std::string s;
        for (std::size_t j = 0; j < g.controls.size(); ++j) s += g.isNegControl(j) ? "negctrl @ " : "ctrl @ ";
        if (inverted) s += "inv @ ";
        s += name;
        if (!g.params.empty()) {
            s += "(";
            for (std::size_t k = 0; k < g.params.size(); ++k) {
                if (!std::isfinite(g.params[k])) return fail(err::Unsupported, std::format("toQasm: gate '{}' has a non-finite parameter", g.name));
                s += (k ? ", " : "") + detail::formatReal(g.params[k]);
            }
            s += ")";
        }
        std::vector<Wire> operands = g.controls;
        operands.insert(operands.end(), g.targets.begin(), g.targets.end());
        QXL_TRY_ASSIGN(std::string ops, wires(operands));
        return s + (ops.empty() ? "" : " " + ops) + ";";
    }

    Status body(const Circuit& c, int depth, std::string& out) const {
        const std::string pad(static_cast<std::size_t>(2 * depth), ' ');
        auto block = [&](const Circuit& sub) -> Status { return body(sub, depth + 1, out); };
        for (NodeId id : c.topologicalOrder()) {
            const Node& n = c.node(id);
            if (const auto* g = std::get_if<Gate>(&n)) {
                QXL_TRY_ASSIGN(std::string line, gate(*g));
                out += pad + line + "\n";
            } else if (const auto* m = std::get_if<Measure>(&n)) {
                QXL_TRY_ASSIGN(std::string q, wire(m->qubit));
                if (m->discards()) { out += pad + "measure " + q + ";\n"; continue; }
                QXL_TRY_ASSIGN(std::string b, bit(m->bit));
                out += std::format("{}{} = measure {};\n", pad, b, q);
            } else if (const auto* r = std::get_if<Reset>(&n)) {
                QXL_TRY_ASSIGN(std::string q, wire(r->qubit));
                out += pad + "reset " + q + ";\n";
            } else if (const auto* b = std::get_if<Barrier>(&n)) {
                QXL_TRY_ASSIGN(std::string ws, wires(b->wires));
                out += pad + "barrier" + (ws.empty() ? "" : " " + ws) + ";\n";
            } else if (const auto* d = std::get_if<Delay>(&n)) {
                QXL_TRY_ASSIGN(std::string ws, wires(d->wires));
                out += std::format("{}delay[{}]{};\n", pad, detail::qasmDuration(d->duration), ws.empty() ? "" : " " + ws);
            } else if (const auto* co = std::get_if<ClassicalOp>(&n)) {
                std::string dst;
                if (co->dst.element) { QXL_TRY_ASSIGN(dst, bit(co->dst.reg.bit(*co->dst.element))); }
                else { QXL_TRY_ASSIGN(dst, span(co->dst.reg)); }
                QXL_TRY_ASSIGN(std::string v, topExpr(co->expr));
                out += std::format("{}{} = {};\n", pad, dst, v);
            } else if (const auto* br = std::get_if<Branch>(&n)) {
                QXL_TRY_ASSIGN(std::string cond, topExpr(br->cond));
                out += std::format("{}if ({}) {{\n", pad, cond);
                QXL_TRY(block(*br->thenBody));
                if ((*br->elseBody).nodeCount() > 0) {
                    out += pad + "} else {\n";
                    QXL_TRY(block(*br->elseBody));
                }
                out += pad + "}\n";
            } else if (const auto* l = std::get_if<Loop>(&n)) {
                QXL_TRY_ASSIGN(std::string cond, topExpr(l->cond));
                out += std::format("{}while ({}) {{\n", pad, cond);
                QXL_TRY(block(*l->body));
                out += pad + "}\n";
            } else if (const auto* bx = std::get_if<Box>(&n)) {
                out += pad + (bx->duration ? std::format("box[{}] {{\n", detail::qasmDuration(*bx->duration)) : std::string("box {\n"));
                QXL_TRY(block(*bx->body));
                out += pad + "}\n";
            }
        }
        return {};
    }

    const Circuit& c_;
    std::vector<std::string> wire_, bit_;
    std::vector<const BitRegister*> regs_;
    std::optional<BitRegister> synthetic_;
    std::string declarations_;
};

} // namespace

Result<std::string> toQasm(const Circuit& c) {
    Emitter e(c);
    return e.run();
}

} // namespace qlab::ir
