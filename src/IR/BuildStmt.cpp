// Spec 14 §2 — declarations, inputs, assignments and measurement lowering.
#include "IR/BuildImpl.hpp"
#include <algorithm>
#include <format>

namespace qlab::ir::build {
namespace {
using lang::BaseType;

RegKind kindOf(BaseType b) {
    switch (b) {
    case BaseType::Bool:
        return RegKind::Bool;
    case BaseType::Int:
        return RegKind::Int;
    case BaseType::Uint:
        return RegKind::Uint;
    default:
        return RegKind::Bit;
    }
}
std::optional<lang::BinaryOp> binaryOf(lang::AssignOp op) {
    using A = lang::AssignOp;
    using B = lang::BinaryOp;
    switch (op) {
    case A::Add:
        return B::Add;
    case A::Sub:
        return B::Sub;
    case A::Mul:
        return B::Mul;
    case A::Div:
        return B::Div;
    case A::Mod:
        return B::Mod;
    case A::Pow:
        return B::Pow;
    case A::BitAnd:
        return B::BitAnd;
    case A::BitOr:
        return B::BitOr;
    case A::BitXor:
        return B::BitXor;
    case A::Shl:
        return B::Shl;
    case A::Shr:
        return B::Shr;
    case A::Set:
        return std::nullopt;
    }
    return std::nullopt;
}
// OpenQASM 3 casts on declaration: int/uint truncate, bool normalises, float/angle promote.
Value coerce(Value v, BaseType t) {
    if (!v.constant() || v.kind == Value::Kind::Dur)
        return v;
    switch (t) {
    case BaseType::Int:
    case BaseType::Uint:
        return Value::ofInt(v.kind == Value::Kind::Float ? static_cast<std::int64_t>(v.f) : v.i);
    case BaseType::Bool:
        return Value::ofInt(v.asDouble() != 0.0 ? 1 : 0);
    case BaseType::Float:
    case BaseType::Angle:
        return Value::ofFloat(v.asDouble());
    default:
        return v;
    }
}
} // namespace

Result<Value> Builder::inputValue(const lang::ClassicalDecl& d) {
    if (auto it = inputs_.find(d.name); it != inputs_.end()) {
        // ParamMap carries doubles (sweep grids): integers are rounded, durations are picoseconds.
        if (d.type.base == BaseType::Duration)
            return Value::ofDuration(Duration{Picoseconds{std::llround(it->second)}, 0});
        if (d.type.base == BaseType::Int || d.type.base == BaseType::Uint ||
            d.type.base == BaseType::Bit)
            return Value::ofInt(std::llround(it->second));
        return coerce(Value::ofFloat(it->second), d.type.base);
    }
    if (d.init) {
        QXL_TRY_ASSIGN(Value v, eval(**d.init));
        return coerce(std::move(v), d.type.base);
    }
    return fail(diagnostic("QL4011", stmtSpan_, d.name));
}

Status Builder::lowerDecl(const lang::ClassicalDecl& d) {
    if (d.io == lang::IoKind::Input) {
        Binding b;
        QXL_TRY_ASSIGN(b.value, inputValue(d));
        bind(d.name, std::move(b));
        return {};
    }
    if (d.type.base != BaseType::Bit && !runtime_.contains(d.name)) {
        Binding b;
        if (d.init) {
            QXL_TRY_ASSIGN(Value v, eval(**d.init));
            b.value = coerce(std::move(v), d.type.base);
        }
        bind(d.name, std::move(b));
        return {};
    }
    // Per-shot storage (spec 14 §2): bit registers always, other scalars when a measurement reaches
    // them.
    std::uint32_t width = 1;
    switch (d.type.base) {
    case BaseType::Bit:
    case BaseType::Bool:
    case BaseType::Int:
    case BaseType::Uint:
        if (d.type.base == BaseType::Int || d.type.base == BaseType::Uint)
            width = 32;
        if (d.type.width && d.type.base != BaseType::Bool) {
            QXL_TRY_ASSIGN(Value w, eval(**d.type.width));
            if (!w.constant() || w.asInt() < 1)
                return fail(err::NotConstant,
                            std::format("'{}' needs a constant positive width", d.name));
            width = static_cast<std::uint32_t>(w.asInt());
        }
        break;
    default:
        return fail(err::Unsupported,
                    std::format("'{}' depends on a measurement but {} has no bit representation",
                                d.name, lang::baseTypeName(d.type.base)));
    }
    Binding b;
    b.kind = Binding::Kind::Storage;
    b.regKind = kindOf(d.type.base);
    b.reg = allocRegister(d.name, width, b.regKind, !d.type.width, scopes_.size() == 1);
    const CregRef reg = b.reg;
    bind(d.name, std::move(b));
    if (!d.init)
        return {};
    if (const auto* m = (*d.init)->as<lang::MeasureExpr>()) {
        lang::Expr self;
        self.node = lang::Ident{d.name};
        return lowerMeasureInto(m->qubits, &self);
    }
    QXL_TRY_ASSIGN(Value v, eval(**d.init));
    emit(ClassicalOp{v.toExpr(), ClassicalTarget{reg, std::nullopt}, {}});
    return {};
}

Status Builder::lowerAssign(const lang::AssignStmt& a) {
    if (const auto* m = a.value->as<lang::MeasureExpr>()) {
        if (a.op != lang::AssignOp::Set)
            return fail(err::Unsupported, "a measurement can only be assigned with '='");
        return lowerMeasureInto(m->qubits, a.target.get());
    }
    QXL_TRY_ASSIGN(Place pl, place(*a.target));
    QXL_TRY_ASSIGN(Value rhs, eval(*a.value));
    const auto op = binaryOf(a.op);
    if (pl.binding->kind == Binding::Kind::Qubits)
        return fail(err::BadBit, "a qubit cannot be assigned");
    if (pl.binding->kind == Binding::Kind::Value) {
        if (conditionScope_ > 0 && pl.scope < conditionScope_)
            return fail(
                err::Unsupported,
                "a compile-time classical variable is assigned inside a run-time branch or loop; "
                "declare it as a bit register so the IR can carry it");
        if (!rhs.constant())
            return fail(
                err::Unsupported,
                "a compile-time classical variable is assigned a value that is only known per "
                "shot; declare it as a bit register so the IR can carry it");
        if (!op) {
            pl.binding->value = std::move(rhs);
            return {};
        }
        QXL_TRY_ASSIGN(pl.binding->value, foldBinary(*op, pl.binding->value, rhs));
        return {};
    }
    ClassicalExpr value = rhs.toExpr();
    if (op) {
        static constexpr std::pair<lang::BinaryOp, ClassOp> kOps[] = {
            {lang::BinaryOp::Add, ClassOp::Add},     {lang::BinaryOp::Sub, ClassOp::Sub},
            {lang::BinaryOp::Mul, ClassOp::Mul},     {lang::BinaryOp::Div, ClassOp::Div},
            {lang::BinaryOp::Mod, ClassOp::Mod},     {lang::BinaryOp::BitAnd, ClassOp::BitAnd},
            {lang::BinaryOp::BitOr, ClassOp::BitOr}, {lang::BinaryOp::BitXor, ClassOp::BitXor},
            {lang::BinaryOp::Shl, ClassOp::Shl},     {lang::BinaryOp::Shr, ClassOp::Shr}};
        auto it = std::find_if(std::begin(kOps), std::end(kOps),
                               [&](const auto& e) { return e.first == *op; });
        if (it == std::end(kOps))
            return fail(err::Unsupported, "'**=' is not available on per-shot classical values");
        ClassicalExpr cur =
            pl.element ? ClassicalExpr::bitRef(pl.reg.bit(*pl.element))
                       : ClassicalExpr::regRef(pl.reg, pl.binding->regKind == RegKind::Int);
        value = ClassicalExpr::binary(it->second, std::move(cur), std::move(value));
    }
    emit(ClassicalOp{std::move(value), ClassicalTarget{pl.reg, pl.element}, {}});
    return {};
}

Status Builder::lowerMeasureInto(const std::vector<lang::ExprPtr>& qubits, const Expr* target) {
    std::vector<Wire> ws;
    for (const auto& q : qubits) {
        QXL_TRY_ASSIGN(auto part, qubitOperand(*q));
        ws.insert(ws.end(), part.begin(), part.end());
    }
    if (!target) {
        for (Wire w : ws)
            emit(Measure{w, kNoBit, std::nullopt, {}});
        return {};
    }
    QXL_TRY_ASSIGN(Place pl, place(*target));
    if (pl.binding->kind != Binding::Kind::Storage || pl.binding->regKind != RegKind::Bit)
        return fail(err::BadBit, "a measurement target must be a bit register");
    std::vector<ClassicalBit> bits;
    if (pl.element)
        bits.push_back(pl.reg.bit(*pl.element));
    else
        for (std::uint32_t i = 0; i < pl.reg.size; ++i)
            bits.push_back(pl.reg.bit(i));
    if (bits.size() != ws.size())
        return fail(err::BadArity, std::format("measure writes {} qubit(s) into {} bit(s)",
                                               ws.size(), bits.size()));
    for (std::size_t i = 0; i < ws.size(); ++i)
        emit(Measure{ws[i], bits[i], std::nullopt, {}});
    return {};
}

} // namespace qlab::ir::build
