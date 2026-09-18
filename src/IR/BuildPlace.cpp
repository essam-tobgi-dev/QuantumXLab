// Spec 14 §2 — operands and places: register-to-wire mapping (little-endian), classical storage
// addressed by assignments, inclusive ranges, and measurement used as a value.
#include "IR/BuildImpl.hpp"
#include <format>

namespace qlab::ir::build {

Result<Builder::Range> Builder::range(const lang::RangeExpr& r) {
    QXL_TRY_ASSIGN(Value lo, eval(*r.start));
    QXL_TRY_ASSIGN(Value hi, eval(*r.stop));
    Value st = Value::ofInt(1);
    if (r.step) { QXL_TRY_ASSIGN(st, eval(**r.step)); }
    if (!lo.constant() || !hi.constant() || !st.constant() || lo.kind == Value::Kind::Dur)
        return fail(err::NotConstant, "range bounds must be known at build time");
    Range out;
    out.start = lo.asInt();
    out.step = st.asInt();
    const std::int64_t stop = hi.asInt();
    if (out.step == 0) return fail(err::BadArity, "range step must not be zero");
    // OpenQASM ranges include both bounds (spec 13 §3): [4:-1:0] is 4, 3, 2, 1, 0.
    if (out.step > 0) out.count = stop < out.start ? 0 : static_cast<std::uint64_t>((stop - out.start) / out.step) + 1;
    else out.count = stop > out.start ? 0 : static_cast<std::uint64_t>((out.start - stop) / -out.step) + 1;
    return out;
}

Result<std::vector<Wire>> Builder::qubitOperand(const Expr& e) {
    using namespace lang;
    if (const auto* n = e.as<PhysicalQubitRef>()) return std::vector<Wire>{Wire{n->index}};
    if (const auto* n = e.as<Ident>()) {
        const Binding* b = lookup(n->name);
        if (!b || b->kind != Binding::Kind::Qubits)
            return fail(err::BadWire, std::format("'{}' is not a qubit operand", n->name));
        return b->qubits;
    }
    if (const auto* n = e.as<IndexExpr>()) {
        QXL_TRY_ASSIGN(auto base, qubitOperand(*n->base));
        QXL_TRY_ASSIGN(Value ix, eval(*n->index));
        if (!ix.constant()) return fail(err::NotConstant, "a qubit index must be known at build time");
        const std::int64_t k = ix.asInt();
        if (k < 0 || static_cast<std::size_t>(k) >= base.size())
            return fail(err::BadWire, std::format("qubit index {} is out of range for an operand of {} qubit(s)", k, base.size()));
        return std::vector<Wire>{base[static_cast<std::size_t>(k)]};
    }
    if (const auto* n = e.as<SliceExpr>()) {
        QXL_TRY_ASSIGN(auto base, qubitOperand(*n->base));
        const auto* r = n->range->as<RangeExpr>();
        if (!r) return fail(err::Unsupported, "a qubit slice must be a range");
        QXL_TRY_ASSIGN(Range rg, range(*r));
        std::vector<Wire> out;
        for (std::uint64_t i = 0; i < rg.count; ++i) {
            const std::int64_t k = rg.start + static_cast<std::int64_t>(i) * rg.step;
            if (k < 0 || static_cast<std::size_t>(k) >= base.size())
                return fail(err::BadWire, std::format("qubit index {} is out of range", k));
            out.push_back(base[static_cast<std::size_t>(k)]);
        }
        return out;
    }
    return fail(err::BadWire, "expression is not a qubit operand");
}

Result<Builder::Place> Builder::place(const Expr& e) {
    using namespace lang;
    if (const auto* n = e.as<Ident>()) {
        const Found f = find(n->name);
        if (!f.binding) return fail(err::BadBit, std::format("'{}' is not declared", n->name));
        return Place{f.binding->reg, std::nullopt, f.binding, f.scope};
    }
    const Expr* baseExpr = nullptr;
    if (const auto* n = e.as<IndexExpr>()) baseExpr = n->base.get();
    if (const auto* n = e.as<SliceExpr>()) baseExpr = n->base.get();
    const auto* id = baseExpr ? baseExpr->as<Ident>() : nullptr;
    const Found f = id ? find(id->name) : Found{};
    if (!f.binding || f.binding->kind != Binding::Kind::Storage)
        return fail(err::BadBit, "only a classical register can be indexed or sliced");
    const CregRef reg = f.binding->reg;
    if (const auto* n = e.as<IndexExpr>()) {
        QXL_TRY_ASSIGN(Value ix, eval(*n->index));
        if (!ix.constant()) return fail(err::NotConstant, "a register index must be known at build time");
        const std::int64_t k = ix.asInt();
        if (k < 0 || k >= static_cast<std::int64_t>(reg.size))
            return fail(err::BadBit, std::format("bit index {} is out of range for '{}'", k, id->name));
        return Place{reg, static_cast<std::uint32_t>(k), f.binding, f.scope};
    }
    const auto* r = e.as<SliceExpr>()->range->as<RangeExpr>();
    if (!r) return fail(err::Unsupported, "a register slice must be a range");
    QXL_TRY_ASSIGN(Range rg, range(*r));
    const std::int64_t last = rg.start + static_cast<std::int64_t>(rg.count) - 1;
    if (rg.step != 1 || rg.count == 0 || rg.start < 0 || last >= static_cast<std::int64_t>(reg.size))
        return fail(err::BadBit, std::format("slice of '{}' must be a non-empty contiguous range inside the register", id->name));
    return Place{CregRef{reg.first + static_cast<std::uint32_t>(rg.start), static_cast<std::uint32_t>(rg.count)},
                 std::nullopt, f.binding, f.scope};
}

Result<Value> Builder::measureValue(const lang::MeasureExpr& m) {
    // `return measure q;` or a measurement inside an expression: the outcome lands in fresh bits.
    std::vector<Wire> ws;
    for (const auto& q : m.qubits) {
        QXL_TRY_ASSIGN(auto part, qubitOperand(*q));
        ws.insert(ws.end(), part.begin(), part.end());
    }
    if (ws.empty()) return fail(err::BadWire, "measurement of no qubits");
    const auto n = static_cast<std::uint32_t>(ws.size());
    const CregRef reg = allocRegister("__meas", n, RegKind::Bit, n == 1, false);
    for (std::uint32_t i = 0; i < n; ++i) emit(Measure{ws[i], reg.bit(i), std::nullopt, {}});
    return n == 1 ? Value::ofSym(ClassicalExpr::bitRef(reg.bit(0))) : Value::ofSym(ClassicalExpr::regRef(reg));
}

} // namespace qlab::ir::build
