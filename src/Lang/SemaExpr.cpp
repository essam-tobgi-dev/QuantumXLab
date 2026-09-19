#include "Lang/Sema.hpp"
#include "Lang/SemaUtil.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

// Spec 13 §3 — constant evaluation, expression typing and expression checks.
namespace qlab::lang {

namespace {
using detail::wrapAdd;
using detail::wrapMul;
using detail::wrapSub;

ConstValue mkInt(std::int64_t v) {
    ConstValue c;
    c.kind = ConstValue::Int;
    c.i = v;
    c.f = static_cast<double>(v);
    return c;
}
ConstValue mkFloat(double v) {
    ConstValue c;
    c.kind = ConstValue::Float;
    c.f = v;
    c.i = detail::saturatingToInt(v);
    return c;
}
ConstValue mkBool(bool v) {
    ConstValue c;
    c.kind = ConstValue::Bool;
    c.i = v;
    c.f = v;
    return c;
}

std::optional<ConstValue> intPow(std::int64_t base, std::int64_t exp) {
    const double r = std::pow(static_cast<double>(base), static_cast<double>(exp));
    // Results beyond the int64 range are not integer constants; keep them as floats.
    if (!std::isfinite(r) || std::abs(r) >= 9.2e18)
        return mkFloat(r);
    return mkInt(static_cast<std::int64_t>(std::llround(r)));
}

std::optional<ConstValue> applyBinary(BinaryOp op, const ConstValue& a, const ConstValue& b) {
    bool bothInt = (a.kind == ConstValue::Int || a.kind == ConstValue::Bool) &&
                   (b.kind == ConstValue::Int || b.kind == ConstValue::Bool);
    const bool aDur = a.kind == ConstValue::DurationPs || a.kind == ConstValue::DurationDt;
    const bool bDur = b.kind == ConstValue::DurationPs || b.kind == ConstValue::DurationDt;
    if (a.kind == ConstValue::DurationPs && b.kind == ConstValue::DurationPs) {
        switch (op) {
        case BinaryOp::Add: {
            ConstValue c = a;
            c.i = wrapAdd(a.i, b.i);
            return c;
        }
        case BinaryOp::Sub: {
            ConstValue c = a;
            c.i = wrapSub(a.i, b.i);
            return c;
        }
        case BinaryOp::Div:
            return b.i ? std::optional(mkFloat(static_cast<double>(a.i) / static_cast<double>(b.i)))
                       : std::nullopt;
        case BinaryOp::Eq:
            return mkBool(a.i == b.i);
        case BinaryOp::Ne:
            return mkBool(a.i != b.i);
        case BinaryOp::Lt:
            return mkBool(a.i < b.i);
        case BinaryOp::Le:
            return mkBool(a.i <= b.i);
        case BinaryOp::Gt:
            return mkBool(a.i > b.i);
        case BinaryOp::Ge:
            return mkBool(a.i >= b.i);
        default:
            return std::nullopt;
        }
    }
    if (aDur && b.isNumeric() && (op == BinaryOp::Mul || op == BinaryOp::Div)) {
        ConstValue c = a;
        double s = b.asDouble();
        if (op == BinaryOp::Div && s == 0)
            return std::nullopt;
        c.i = detail::saturatingToInt(std::round(
            op == BinaryOp::Mul ? static_cast<double>(a.i) * s : static_cast<double>(a.i) / s));
        return c;
    }
    if (a.isNumeric() && bDur && op == BinaryOp::Mul) {
        ConstValue c = b;
        c.i = detail::saturatingToInt(std::round(static_cast<double>(b.i) * a.asDouble()));
        return c;
    }
    if (!a.isNumeric() || !b.isNumeric())
        return std::nullopt;
    double x = a.asDouble(), y = b.asDouble();
    switch (op) {
    case BinaryOp::Add:
        return bothInt ? mkInt(wrapAdd(a.i, b.i)) : mkFloat(x + y);
    case BinaryOp::Sub:
        return bothInt ? mkInt(wrapSub(a.i, b.i)) : mkFloat(x - y);
    case BinaryOp::Mul:
        return bothInt ? mkInt(wrapMul(a.i, b.i)) : mkFloat(x * y);
    case BinaryOp::Div:
        if (y == 0)
            return std::nullopt;
        if (!bothInt)
            return mkFloat(x / y);
        return b.i == -1 ? mkInt(wrapSub(0, a.i)) : mkInt(a.i / b.i); // INT64_MIN / -1 wraps
    case BinaryOp::Mod:
        if (y == 0)
            return std::nullopt;
        if (!bothInt)
            return mkFloat(std::fmod(x, y));
        return b.i == -1 ? mkInt(0) : mkInt(a.i % b.i);
    case BinaryOp::Pow:
        return (bothInt && b.i >= 0) ? intPow(a.i, b.i) : std::optional(mkFloat(std::pow(x, y)));
    case BinaryOp::Eq:
        return mkBool(x == y);
    case BinaryOp::Ne:
        return mkBool(x != y);
    case BinaryOp::Lt:
        return mkBool(x < y);
    case BinaryOp::Le:
        return mkBool(x <= y);
    case BinaryOp::Gt:
        return mkBool(x > y);
    case BinaryOp::Ge:
        return mkBool(x >= y);
    case BinaryOp::And:
        return mkBool(x != 0 && y != 0);
    case BinaryOp::Or:
        return mkBool(x != 0 || y != 0);
    case BinaryOp::BitAnd:
        return bothInt ? std::optional(mkInt(a.i & b.i)) : std::nullopt;
    case BinaryOp::BitOr:
        return bothInt ? std::optional(mkInt(a.i | b.i)) : std::nullopt;
    case BinaryOp::BitXor:
        return bothInt ? std::optional(mkInt(a.i ^ b.i)) : std::nullopt;
    case BinaryOp::Shl:
    case BinaryOp::Shr:
        // Negative shift counts have no value; counts past the width shift every bit out.
        if (!bothInt || b.i < 0)
            return std::nullopt;
        if (op == BinaryOp::Shl)
            return mkInt(
                b.i >= 64 ? 0 : static_cast<std::int64_t>(static_cast<std::uint64_t>(a.i) << b.i));
        return mkInt(b.i >= 64 ? (a.i < 0 ? -1 : 0) : a.i >> b.i);
    }
    return std::nullopt;
}
} // namespace

std::optional<ConstValue> Sema::fold(const Expr& e) {
    if (auto* n = e.as<IntLit>())
        return mkInt(n->value);
    if (auto* n = e.as<FloatLit>())
        return mkFloat(n->value);
    if (auto* n = e.as<BoolLit>())
        return mkBool(n->value);
    if (auto* n = e.as<BitStringLit>()) {
        ConstValue c;
        c.kind = ConstValue::Bits;
        c.bits = n->bits;
        std::uint64_t v = 0;
        for (char b : n->bits)
            if (b != '_')
                v = (v << 1) | (b == '1' ? 1u : 0u);
        c.i = static_cast<std::int64_t>(v);
        c.f = static_cast<double>(v);
        return c;
    }
    if (auto* n = e.as<DurationLit>()) {
        ConstValue c;
        c.kind = n->unit == DurationUnit::Dt ? ConstValue::DurationDt : ConstValue::DurationPs;
        c.i = durationToPs(n->value, n->unit);
        c.f = static_cast<double>(c.i);
        return c;
    }
    if (auto* n = e.as<ConstantRef>()) {
        if (n->name == "pi")
            return mkFloat(std::numbers::pi);
        if (n->name == "tau")
            return mkFloat(2 * std::numbers::pi);
        if (n->name == "euler")
            return mkFloat(std::numbers::e);
        return std::nullopt;
    }
    if (auto* n = e.as<Ident>()) {
        const Symbol* s = lookup(n->name);
        if (s && s->value)
            return s->value;
        return std::nullopt;
    }
    if (auto* n = e.as<UnaryExpr>()) {
        auto v = fold(*n->operand);
        if (!v)
            return std::nullopt;
        switch (n->op) {
        case UnaryOp::Neg:
            if (v->kind == ConstValue::DurationPs || v->kind == ConstValue::DurationDt) {
                v->i = wrapSub(0, v->i);
                v->f = -v->f;
                return v;
            }
            return v->kind == ConstValue::Float ? mkFloat(-v->f) : mkInt(wrapSub(0, v->i));
        case UnaryOp::Not:
            return mkBool(v->asDouble() == 0);
        case UnaryOp::BitNot:
            return v->kind == ConstValue::Int ? std::optional(mkInt(~v->i)) : std::nullopt;
        }
    }
    if (auto* n = e.as<BinaryExpr>()) {
        auto a = fold(*n->lhs);
        auto b = fold(*n->rhs);
        if (!a || !b)
            return std::nullopt;
        return applyBinary(n->op, *a, *b);
    }
    if (auto* n = e.as<CastExpr>()) {
        auto v = fold(*n->operand);
        if (!v)
            return std::nullopt;
        switch (n->type.base) {
        case BaseType::Int:
        case BaseType::Uint:
            return mkInt(detail::saturatingToInt(v->asDouble()));
        case BaseType::Float:
        case BaseType::Angle:
            return mkFloat(v->asDouble());
        case BaseType::Bool:
            return mkBool(v->asDouble() != 0);
        default:
            return std::nullopt;
        }
    }
    if (auto* n = e.as<CallExpr>()) {
        std::vector<ConstValue> args;
        for (auto& a : n->args) {
            auto v = fold(*a);
            if (!v || !v->isNumeric())
                return std::nullopt;
            args.push_back(*v);
        }
        const std::string& f = n->callee;
        auto one = [&](double (*fn)(double)) -> std::optional<ConstValue> {
            return args.size() == 1 ? std::optional(mkFloat(fn(args[0].asDouble()))) : std::nullopt;
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
        if (f == "pow" && args.size() == 2)
            return mkFloat(std::pow(args[0].asDouble(), args[1].asDouble()));
        if (f == "mod" && args.size() == 2)
            return mkFloat(std::fmod(args[0].asDouble(), args[1].asDouble()));
        if (f == "popcount" && args.size() == 1)
            return mkInt(
                static_cast<std::int64_t>(std::popcount(static_cast<std::uint64_t>(args[0].i))));
        return std::nullopt;
    }
    return std::nullopt;
}

SemaType Sema::typeOf(const Expr& e) {
    SemaType t;
    auto scalar = [](BaseType b) {
        SemaType s;
        s.base = b;
        return s;
    };
    if (e.is<ErrorExpr>())
        return t; // Void: an already-reported syntax error never cascades
    if (e.is<IntLit>())
        return scalar(BaseType::Int);
    if (e.is<FloatLit>() || e.is<ConstantRef>())
        return scalar(BaseType::Float);
    if (e.is<ImagLit>())
        return scalar(BaseType::Complex);
    if (e.is<BoolLit>())
        return scalar(BaseType::Bool);
    if (auto* n = e.as<BitStringLit>()) {
        SemaType s = scalar(BaseType::Bit);
        s.isRegister = n->bits.size() != 1;
        s.size = n->bits.size();
        return s;
    }
    if (e.is<DurationLit>())
        return scalar(BaseType::Duration);
    if (e.is<PhysicalQubitRef>())
        return scalar(BaseType::Qubit);
    if (auto* n = e.as<Ident>()) {
        const Symbol* s = lookup(n->name);
        return s ? s->type : t;
    }
    if (auto* n = e.as<IndexExpr>()) {
        SemaType b = typeOf(*n->base);
        b.isRegister = false;
        b.size = 1;
        return b;
    }
    if (auto* n = e.as<SliceExpr>()) {
        SemaType b = typeOf(*n->base);
        return b;
    }
    if (auto* n = e.as<UnaryExpr>()) {
        SemaType o = typeOf(*n->operand);
        if (n->op == UnaryOp::Not)
            return scalar(BaseType::Bool);
        return o;
    }
    if (auto* n = e.as<BinaryExpr>()) {
        SemaType a = typeOf(*n->lhs), b = typeOf(*n->rhs);
        switch (n->op) {
        case BinaryOp::Eq:
        case BinaryOp::Ne:
        case BinaryOp::Lt:
        case BinaryOp::Le:
        case BinaryOp::Gt:
        case BinaryOp::Ge:
        case BinaryOp::And:
        case BinaryOp::Or:
            return scalar(BaseType::Bool);
        default:
            break;
        }
        // An operand of unknown type (undeclared name, syntax error) leaves the result unknown, so
        // the error already reported for it does not reappear as a type mismatch.
        if (a.base == BaseType::Void || b.base == BaseType::Void)
            return t;
        if (a.base == BaseType::Duration || b.base == BaseType::Duration)
            return (n->op == BinaryOp::Div && a.base == BaseType::Duration &&
                    b.base == BaseType::Duration)
                       ? scalar(BaseType::Float)
                       : scalar(BaseType::Duration);
        if (a.base == BaseType::Float || b.base == BaseType::Float || a.base == BaseType::Angle ||
            b.base == BaseType::Angle)
            return scalar(a.base == BaseType::Angle && b.base == BaseType::Angle ? BaseType::Angle
                                                                                 : BaseType::Float);
        if (a.base == BaseType::Bit && b.base == BaseType::Bit)
            return a;
        return scalar(BaseType::Int);
    }
    if (auto* n = e.as<CastExpr>()) {
        SemaType s = scalar(n->type.base);
        if (n->type.width) {
            if (auto w = fold(**n->type.width); w && w->i > 0) {
                if (n->type.base == BaseType::Bit) {
                    s.isRegister = true;
                    s.size = static_cast<std::size_t>(w->i);
                } else
                    s.width = static_cast<int>(std::min<std::int64_t>(w->i, 1 << 16));
            }
        }
        return s;
    }
    if (auto* n = e.as<CallExpr>()) {
        const std::string& f = n->callee;
        if (f == "popcount" || f == "sizeof" || f == "rotl" || f == "rotr")
            return scalar(BaseType::Int);
        const Symbol* s = lookup(f);
        if (s && s->kind == SymbolKind::Def)
            return s->type;
        return scalar(BaseType::Float);
    }
    if (auto* n = e.as<MeasureExpr>()) {
        SemaType s = scalar(BaseType::Bit);
        std::size_t total = 0;
        for (auto& q : n->qubits) {
            SemaType qt = typeOf(*q);
            total += qt.isRegister ? qt.size : 1;
        }
        s.isRegister = total > 1;
        s.size = total;
        return s;
    }
    if (auto* n = e.as<ConcatExpr>()) {
        SemaType a = typeOf(*n->lhs), b = typeOf(*n->rhs);
        a.isRegister = true;
        a.size = (a.isRegister ? a.size : 1) + (b.isRegister ? b.size : 1);
        return a;
    }
    if (e.is<DurationOfExpr>())
        return scalar(BaseType::Duration);
    if (e.is<SetExpr>() || e.is<RangeExpr>())
        return scalar(BaseType::Int);
    return t;
}

} // namespace qlab::lang
