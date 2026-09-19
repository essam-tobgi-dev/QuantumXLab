// Spec 14 §2 — expression evaluation: constants folded, per-shot values kept as ClassicalExpr.
#include "IR/BuildImpl.hpp"
#include <bit>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::ir::build {
namespace {
using lang::BinaryOp;

std::optional<ClassOp> classOpOf(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add:
        return ClassOp::Add;
    case BinaryOp::Sub:
        return ClassOp::Sub;
    case BinaryOp::Mul:
        return ClassOp::Mul;
    case BinaryOp::Div:
        return ClassOp::Div;
    case BinaryOp::Mod:
        return ClassOp::Mod;
    case BinaryOp::Eq:
        return ClassOp::Eq;
    case BinaryOp::Ne:
        return ClassOp::Ne;
    case BinaryOp::Lt:
        return ClassOp::Lt;
    case BinaryOp::Le:
        return ClassOp::Le;
    case BinaryOp::Gt:
        return ClassOp::Gt;
    case BinaryOp::Ge:
        return ClassOp::Ge;
    case BinaryOp::And:
        return ClassOp::LogicAnd;
    case BinaryOp::Or:
        return ClassOp::LogicOr;
    case BinaryOp::BitAnd:
        return ClassOp::BitAnd;
    case BinaryOp::BitOr:
        return ClassOp::BitOr;
    case BinaryOp::BitXor:
        return ClassOp::BitXor;
    case BinaryOp::Shl:
        return ClassOp::Shl;
    case BinaryOp::Shr:
        return ClassOp::Shr;
    case BinaryOp::Pow:
        return std::nullopt;
    }
    return std::nullopt;
}

Result<Value> durationArithmetic(BinaryOp op, const Value& a, const Value& b) {
    using K = Value::Kind;
    if (a.kind == K::Dur && b.kind == K::Dur) {
        switch (op) {
        case BinaryOp::Add:
            return Value::ofDuration(Duration{a.dur.ps + b.dur.ps, a.dur.dt + b.dur.dt});
        case BinaryOp::Sub:
            return Value::ofDuration(Duration{a.dur.ps - b.dur.ps, a.dur.dt - b.dur.dt});
        case BinaryOp::Eq:
            return Value::ofInt(a.dur == b.dur);
        case BinaryOp::Ne:
            return Value::ofInt(a.dur != b.dur);
        // OpenQASM 3 defines the ratio of two durations as a dimensionless float (spec 13 §3).
        // It is what a program needs to turn a swept delay into a phase, so it must fold here.
        case BinaryOp::Div: {
            // A ratio is only meaningful when both sides are in the same units: mixing a
            // wall-clock duration with a symbolic `dt` one needs the device's `dt`, which is not
            // known until the compiler binds a device (QL4010 there).
            const bool aSym = a.dur.symbolic(), bSym = b.dur.symbolic();
            if (aSym != bSym || (aSym && a.dur.ps.get() != 0) || (bSym && b.dur.ps.get() != 0))
                return fail(err::NotConstant,
                            "the ratio of a 'dt' duration and a wall-clock duration is not known "
                            "until a device is selected");
            const double num =
                aSym ? static_cast<double>(a.dur.dt) : static_cast<double>(a.dur.ps.get());
            const double den =
                bSym ? static_cast<double>(b.dur.dt) : static_cast<double>(b.dur.ps.get());
            if (den == 0.0)
                return fail(err::Unsupported, "division by a zero duration");
            return Value::ofFloat(num / den);
        }
        case BinaryOp::Lt:
            return Value::ofInt(a.dur < b.dur);
        case BinaryOp::Le:
            return Value::ofInt(a.dur <= b.dur);
        case BinaryOp::Gt:
            return Value::ofInt(a.dur > b.dur);
        case BinaryOp::Ge:
            return Value::ofInt(a.dur >= b.dur);
        default:
            return fail(err::Unsupported, "unsupported arithmetic on two durations");
        }
    }
    // Scaling by a number: both the picosecond and the dt part scale (spec 13 §3 durations).
    const Value& d = a.kind == K::Dur ? a : b;
    const double k = (a.kind == K::Dur ? b : a).asDouble();
    const auto scaled = [&](double s) {
        return Value::ofDuration(
            Duration{Picoseconds{std::llround(static_cast<double>(d.dur.ps.get()) * s)},
                     std::llround(static_cast<double>(d.dur.dt) * s)});
    };
    if (op == BinaryOp::Mul)
        return scaled(k);
    if (op == BinaryOp::Div && a.kind == K::Dur && k != 0.0)
        return scaled(1.0 / k);
    return fail(err::Unsupported, "unsupported duration arithmetic");
}

Result<Value> mathCall(const std::string& f, const std::vector<Value>& a) {
    auto one = [&](double (*fn)(double)) -> Result<Value> {
        if (a.size() != 1)
            return fail(err::BadArity, std::format("'{}' takes one argument", f));
        return Value::ofFloat(fn(a[0].asDouble()));
    };
    if (f == "sin")
        return one(std::sin);
    if (f == "cos")
        return one(std::cos);
    if (f == "tan")
        return one(std::tan);
    if (f == "arcsin")
        return one(std::asin);
    if (f == "arccos")
        return one(std::acos);
    if (f == "arctan")
        return one(std::atan);
    if (f == "exp")
        return one(std::exp);
    if (f == "ln" || f == "log")
        return one(std::log);
    if (f == "sqrt")
        return one(std::sqrt);
    if (f == "floor")
        return one(std::floor);
    if (f == "ceil")
        return one(std::ceil);
    if (f == "pow" && a.size() == 2)
        return Value::ofFloat(std::pow(a[0].asDouble(), a[1].asDouble()));
    if (f == "mod" && a.size() == 2)
        return Value::ofFloat(std::fmod(a[0].asDouble(), a[1].asDouble()));
    if (f == "popcount" && a.size() == 1)
        return Value::ofInt(std::popcount(static_cast<std::uint64_t>(a[0].asInt())));
    return fail(err::Unsupported, std::format("'{}' cannot be evaluated at build time", f));
}
} // namespace

Result<Value> foldBinary(BinaryOp op, const Value& a, const Value& b) {
    using K = Value::Kind;
    if (a.kind == K::Dur || b.kind == K::Dur)
        return durationArithmetic(op, a, b);
    const bool bothInt = a.kind == K::Int && b.kind == K::Int;
    const double x = a.asDouble(), y = b.asDouble();
    const bool shiftOk = b.asInt() >= 0 && b.asInt() < 63;
    switch (op) {
    case BinaryOp::Add:
        return bothInt ? Value::ofInt(a.i + b.i) : Value::ofFloat(x + y);
    case BinaryOp::Sub:
        return bothInt ? Value::ofInt(a.i - b.i) : Value::ofFloat(x - y);
    case BinaryOp::Mul:
        return bothInt ? Value::ofInt(a.i * b.i) : Value::ofFloat(x * y);
    case BinaryOp::Div:
        if (y == 0)
            return fail(err::NotConstant, "division by zero in a compile-time expression");
        return bothInt ? Value::ofInt(a.i / b.i) : Value::ofFloat(x / y);
    case BinaryOp::Mod:
        if (y == 0)
            return fail(err::NotConstant, "modulo by zero in a compile-time expression");
        return bothInt ? Value::ofInt(a.i % b.i) : Value::ofFloat(std::fmod(x, y));
    case BinaryOp::Pow:
        return (bothInt && b.i >= 0) ? Value::ofInt(std::llround(std::pow(x, y)))
                                     : Value::ofFloat(std::pow(x, y));
    case BinaryOp::Eq:
        return Value::ofInt(x == y);
    case BinaryOp::Ne:
        return Value::ofInt(x != y);
    case BinaryOp::Lt:
        return Value::ofInt(x < y);
    case BinaryOp::Le:
        return Value::ofInt(x <= y);
    case BinaryOp::Gt:
        return Value::ofInt(x > y);
    case BinaryOp::Ge:
        return Value::ofInt(x >= y);
    case BinaryOp::And:
        return Value::ofInt(x != 0 && y != 0);
    case BinaryOp::Or:
        return Value::ofInt(x != 0 || y != 0);
    case BinaryOp::BitAnd:
        return Value::ofInt(a.asInt() & b.asInt());
    case BinaryOp::BitOr:
        return Value::ofInt(a.asInt() | b.asInt());
    case BinaryOp::BitXor:
        return Value::ofInt(a.asInt() ^ b.asInt());
    case BinaryOp::Shl:
    case BinaryOp::Shr:
        if (!shiftOk)
            return fail(err::NotConstant, "shift count out of range in a compile-time expression");
        return Value::ofInt(op == BinaryOp::Shl ? a.asInt() << b.asInt() : a.asInt() >> b.asInt());
    }
    return fail(err::Unsupported, "unsupported binary operator");
}

Result<Value> Builder::eval(const Expr& e) {
    using namespace lang;
    if (const auto* n = e.as<IntLit>())
        return Value::ofInt(n->value);
    if (const auto* n = e.as<FloatLit>())
        return Value::ofFloat(n->value);
    if (const auto* n = e.as<BoolLit>())
        return Value::ofInt(n->value ? 1 : 0);
    if (const auto* n = e.as<BitStringLit>()) { // MSB first, as written
        std::int64_t v = 0;
        for (char b : n->bits)
            if (b != '_')
                v = (v << 1) | (b == '1' ? 1 : 0);
        return Value::ofInt(v);
    }
    if (const auto* n = e.as<DurationLit>()) {
        const std::int64_t raw = lang::durationToPs(n->value, n->unit);
        return Value::ofDuration(n->unit == DurationUnit::Dt ? Duration{Picoseconds{0}, raw}
                                                             : Duration{Picoseconds{raw}, 0});
    }
    if (const auto* n = e.as<ConstantRef>()) {
        if (n->name == "pi")
            return Value::ofFloat(std::numbers::pi);
        if (n->name == "tau")
            return Value::ofFloat(2 * std::numbers::pi);
        if (n->name == "euler")
            return Value::ofFloat(std::numbers::e);
        return fail(err::NotConstant, std::format("unknown constant '{}'", n->name));
    }
    if (const auto* n = e.as<Ident>()) {
        const Binding* b = lookup(n->name);
        if (!b)
            return fail(err::NotConstant, std::format("'{}' is not bound at build time", n->name));
        if (b->kind == Binding::Kind::Value)
            return b->value;
        if (b->kind == Binding::Kind::Qubits)
            return fail(err::BadBit, std::format("'{}' is a qubit", n->name));
        const bool bitLike = b->regKind == RegKind::Bit || b->regKind == RegKind::Bool;
        return bitLike && b->reg.size == 1
                   ? Value::ofSym(ClassicalExpr::bitRef(b->reg.bit(0)))
                   : Value::ofSym(ClassicalExpr::regRef(b->reg, b->regKind == RegKind::Int));
    }
    if (e.is<IndexExpr>() || e.is<SliceExpr>()) {
        QXL_TRY_ASSIGN(Place pl, place(e));
        if (pl.binding->kind != Binding::Kind::Storage)
            return fail(err::BadBit, "only a classical register can be indexed in an expression");
        if (pl.element)
            return Value::ofSym(ClassicalExpr::bitRef(pl.reg.bit(*pl.element)));
        return Value::ofSym(ClassicalExpr::regRef(pl.reg, pl.binding->regKind == RegKind::Int));
    }
    if (const auto* n = e.as<MeasureExpr>())
        return measureValue(*n);
    if (const auto* n = e.as<UnaryExpr>()) {
        QXL_TRY_ASSIGN(Value v, eval(*n->operand));
        if (v.constant()) {
            switch (n->op) {
            case UnaryOp::Neg:
                if (v.kind == Value::Kind::Dur)
                    return Value::ofDuration(Duration{Picoseconds{-v.dur.ps.get()}, -v.dur.dt});
                return v.kind == Value::Kind::Float ? Value::ofFloat(-v.f) : Value::ofInt(-v.i);
            case UnaryOp::Not:
                return Value::ofInt(v.asDouble() == 0 ? 1 : 0);
            case UnaryOp::BitNot:
                return Value::ofInt(~v.asInt());
            }
        }
        const ClassOp op = n->op == UnaryOp::Neg   ? ClassOp::Neg
                           : n->op == UnaryOp::Not ? ClassOp::LogicNot
                                                   : ClassOp::BitNot;
        return Value::ofSym(ClassicalExpr::unary(op, std::move(v.sym)));
    }
    if (const auto* n = e.as<BinaryExpr>()) {
        QXL_TRY_ASSIGN(Value a, eval(*n->lhs));
        QXL_TRY_ASSIGN(Value b, eval(*n->rhs));
        if (a.constant() && b.constant())
            return foldBinary(n->op, a, b);
        auto op = classOpOf(n->op);
        if (!op)
            return fail(err::Unsupported, "'**' is not available on per-shot classical values");
        return Value::ofSym(ClassicalExpr::binary(*op, a.toExpr(), b.toExpr()));
    }
    if (const auto* n = e.as<CastExpr>()) {
        QXL_TRY_ASSIGN(Value v, eval(*n->operand));
        if (!v.constant()) {
            if (n->type.base == BaseType::Bool)
                return Value::ofSym(ClassicalExpr::binary(ClassOp::Ne, std::move(v.sym),
                                                          ClassicalExpr::constant(0)));
            return v; // int/uint/bit casts keep the bit pattern
        }
        switch (n->type.base) {
        case BaseType::Int:
        case BaseType::Uint:
        case BaseType::Bit:
            return Value::ofInt(v.asInt());
        case BaseType::Float:
        case BaseType::Angle:
            return Value::ofFloat(v.asDouble());
        case BaseType::Bool:
            return Value::ofInt(v.asDouble() != 0 ? 1 : 0);
        default:
            return fail(err::Unsupported, "unsupported cast");
        }
    }
    if (const auto* n = e.as<CallExpr>()) {
        if (auto it = p_.defs.find(n->callee); it != p_.defs.end())
            return callDef(*it->second, n->args);
        std::vector<Value> args;
        for (const auto& a : n->args) {
            QXL_TRY_ASSIGN(Value v, eval(*a));
            if (!v.constant())
                return fail(err::NotConstant,
                            std::format("'{}' needs arguments known at build time", n->callee));
            args.push_back(std::move(v));
        }
        return mathCall(n->callee, args);
    }
    return fail(err::Unsupported, "expression cannot be evaluated at build time");
}

Result<double> Builder::evalDouble(const Expr& e) {
    QXL_TRY_ASSIGN(Value v, eval(e));
    if (!v.constant() || v.kind == Value::Kind::Dur)
        return fail(err::NotConstant, "gate parameters must be numbers known at build time");
    return v.asDouble();
}

Result<Duration> Builder::evalDuration(const Expr& e) {
    QXL_TRY_ASSIGN(Value v, eval(e));
    if (v.kind == Value::Kind::Dur)
        return v.dur;
    if (v.constant() && v.asDouble() == 0)
        return Duration{};
    return fail(err::BadArity, "expected a duration");
}

} // namespace qlab::ir::build
