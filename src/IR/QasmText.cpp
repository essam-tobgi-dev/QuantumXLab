// Spec 13 §6, 14 §11 — OpenQASM text for numbers, durations and calibration blocks. Calibrations
// are carried from the program to the circuit metadata so that toQasm re-emits them intact.
#include "IR/Emit.hpp"
#include <format>
#include <set>
#include <variant>

namespace qlab::ir::detail {

std::string formatReal(double v) {
    if (v == 0.0) return "0";   // also −0: the lexer has no signed zero
    return std::format("{}", v);
}

std::string formatFloatLiteral(double v) {
    std::string s = formatReal(v);
    if (s.find_first_of(".en") == std::string::npos) s += ".0";   // 'n' covers inf and nan
    return s;
}

namespace {
std::uint64_t magnitude(std::int64_t v) {
    return v < 0 ? 0 - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
}
// 1500 ps -> "1.5ns": the lexer's ns scale is exact to the picosecond (Token.hpp durationToPs).
std::string nanoseconds(std::int64_t ps) {
    const std::uint64_t u = magnitude(ps);
    std::string s = std::format("{}{}", ps < 0 ? "-" : "", u / 1000);
    if (const std::uint64_t frac = u % 1000; frac != 0) {
        std::string f = std::format("{:03}", frac);
        while (f.back() == '0') f.pop_back();
        s += "." + f;
    }
    return s + "ns";
}

template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };

struct Printer {
    const NameResolver& resolve;
    std::set<std::string> locals;   // defcal parameters are never substituted

    std::string list(const std::vector<lang::ExprPtr>& v) const {
        std::string s;
        for (std::size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + expr(v[i]);
        return s;
    }
    std::string expr(const lang::ExprPtr& e) const { return e ? expr(*e) : std::string("0"); }
    std::string expr(const lang::Expr& e) const {
        using namespace lang;
        if (const auto* n = e.as<IntLit>()) return std::format("{}", n->value);
        if (const auto* n = e.as<FloatLit>()) return formatFloatLiteral(n->value);
        if (const auto* n = e.as<ImagLit>()) return formatFloatLiteral(n->value) + "im";
        if (const auto* n = e.as<BoolLit>()) return n->value ? "true" : "false";
        if (const auto* n = e.as<BitStringLit>()) return "\"" + n->bits + "\"";
        if (const auto* n = e.as<DurationLit>()) return formatReal(n->value) + durationUnitName(n->unit);
        if (const auto* n = e.as<ConstantRef>()) return n->name;
        if (const auto* n = e.as<Ident>()) {
            if (!locals.contains(n->name) && resolve)
                if (auto lit = resolve(n->name)) return *lit;
            return n->name;
        }
        if (const auto* n = e.as<PhysicalQubitRef>()) return std::format("${}", n->index);
        if (const auto* n = e.as<UnaryExpr>()) {
            // A numeric literal takes the sign directly: printing `-(26.0)` for an input that was
            // substituted as `-26.0` makes emission non-idempotent (the text re-parses as a unary
            // expression and grows another pair of brackets on every round trip).
            const std::string inner = expr(n->operand);
            const bool literal = n->operand && (n->operand->template as<IntLit>() != nullptr ||
                                                n->operand->template as<FloatLit>() != nullptr);
            if (literal) return std::format("{}{}", unaryOpName(n->op), inner);
            return std::format("{}({})", unaryOpName(n->op), inner);
        }
        if (const auto* n = e.as<BinaryExpr>()) return std::format("({} {} {})", expr(n->lhs), binaryOpName(n->op), expr(n->rhs));
        if (const auto* n = e.as<CallExpr>()) return std::format("{}({})", n->callee, list(n->args));
        if (const auto* n = e.as<CastExpr>())
            return std::format("{}{}({})", baseTypeName(n->type.base), n->type.width ? "[" + expr(*n->type.width) + "]" : "", expr(n->operand));
        if (const auto* n = e.as<IndexExpr>()) return std::format("{}[{}]", expr(n->base), expr(n->index));
        if (const auto* n = e.as<RangeExpr>())
            return n->step ? std::format("{}:{}:{}", expr(n->start), expr(*n->step), expr(n->stop))
                           : std::format("{}:{}", expr(n->start), expr(n->stop));
        if (const auto* n = e.as<SliceExpr>()) return std::format("{}[{}]", expr(n->base), expr(n->range));
        if (const auto* n = e.as<SetExpr>()) return "{" + list(n->items) + "}";
        if (const auto* n = e.as<ConcatExpr>()) return std::format("({} ++ {})", expr(n->lhs), expr(n->rhs));
        if (const auto* n = e.as<MeasureExpr>()) return "measure " + list(n->qubits);
        return "0";   // durationof / error placeholders have no source form
    }
    std::string waveform(const lang::PulseWaveform& w) const {
        if (w.kind == "samples") return "[" + list(w.args) + "]";
        if (w.kind == "ref") {
            const auto* id = w.args.empty() || !w.args.front() ? nullptr : w.args.front()->as<lang::Ident>();
            return id ? id->name : std::string("?");
        }
        return std::format("{}({})", w.kind, list(w.args));
    }
    static std::string names(const std::vector<std::string>& v) {
        std::string s;
        for (std::size_t i = 0; i < v.size(); ++i) s += (i ? ", " : " ") + v[i];
        return s;
    }
    std::string body(const std::vector<lang::PulseStmt>& stmts) const {
        using namespace lang;
        std::string out;
        for (const auto& ps : stmts) {
            out += "  ";
            out += std::visit(
                Overloaded{
                    [&](const PulsePortDecl& d) { return std::format("extern port {};", d.name); },
                    [&](const PulseFrameDecl& f) {
                        return std::format("frame {} = newframe({}, {}, {});", f.name, f.port, expr(f.frequency), expr(f.phase));
                    },
                    [&](const PulseWaveformDecl& w) { return std::format("waveform {} = {};", w.name, waveform(w.waveform)); },
                    [&](const PulsePlay& p) {
                        const auto* named = std::get_if<std::string>(&p.waveform);
                        return std::format("play({}, {});", p.frame, named ? *named : waveform(std::get<PulseWaveform>(p.waveform)));
                    },
                    [&](const PulseFrameOp& o) { return std::format("{}({}, {});", o.op, o.frame, expr(o.value)); },
                    [&](const PulseDelay& d) { return std::format("delay[{}]{};", expr(d.duration), names(d.frames)); },
                    [&](const PulseCapture& c) {
                        std::string call = std::format("capture_v2({}, {});", c.frame, expr(c.duration));
                        if (!c.target) return call;
                        return *c.target == "return" ? "return " + call : *c.target + " = " + call;
                    },
                    [&](const PulseBarrier& b) { return std::format("barrier{};", names(b.frames)); },
                },
                ps.node);
            out += "\n";
        }
        return out;
    }
};
} // namespace

std::string qasmDuration(const Duration& d) {
    std::string out;
    if (d.ps.get() != 0 || d.dt == 0) out = nanoseconds(d.ps.get());
    if (d.dt != 0) {
        if (out.empty()) out = std::format("{}{}dt", d.dt < 0 ? "-" : "", magnitude(d.dt));
        else out += std::format(" {} {}dt", d.dt < 0 ? "-" : "+", magnitude(d.dt));
    }
    return out;
}

std::string calibrationText(const lang::Stmt& s, const NameResolver& resolve) {
    Printer pr{resolve, {}};
    if (const auto* c = s.as<lang::CalStmt>()) return "cal {\n" + pr.body(c->body) + "}";
    const auto* d = s.as<lang::DefcalStmt>();
    if (!d) return {};
    std::string head = "defcal " + d->gate;
    if (!d->params.empty()) {
        std::string ps;
        for (std::size_t i = 0; i < d->params.size(); ++i) {
            const bool named = i < d->paramNames.size() && !d->paramNames[i].empty();
            if (named) pr.locals.insert(d->paramNames[i]);
            ps += (i ? ", " : "") + (named ? "angle " + d->paramNames[i] : pr.expr(d->params[i]));
        }
        head += "(" + ps + ")";
    }
    for (std::size_t i = 0; i < d->qubits.size(); ++i) head += std::format("{}${}", i ? ", " : " ", d->qubits[i].index);
    if (d->returnType.base != lang::BaseType::Void)
        head += std::format(" -> {}{}", lang::baseTypeName(d->returnType.base),
                            d->returnType.width ? "[" + pr.expr(*d->returnType.width) + "]" : "");
    return head + " {\n" + pr.body(d->body) + "}";
}

} // namespace qlab::ir::detail
