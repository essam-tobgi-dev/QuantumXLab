// Spec 14 §3 — classical expressions over the flat bit space, and their per-shot evaluation.
#include "IR/Node.hpp"
#include <algorithm>
#include <format>

namespace qlab::ir {

const char* classOpName(ClassOp op) {
    switch (op) {
    case ClassOp::Const: return "const";
    case ClassOp::BitRef: return "bit";
    case ClassOp::RegRef: return "reg";
    case ClassOp::Eq: return "==";
    case ClassOp::Ne: return "!=";
    case ClassOp::Lt: return "<";
    case ClassOp::Le: return "<=";
    case ClassOp::Gt: return ">";
    case ClassOp::Ge: return ">=";
    case ClassOp::LogicAnd: return "&&";
    case ClassOp::LogicOr: return "||";
    case ClassOp::LogicNot: return "!";
    case ClassOp::BitAnd: return "&";
    case ClassOp::BitOr: return "|";
    case ClassOp::BitXor: return "^";
    case ClassOp::BitNot: return "~";
    case ClassOp::Add: return "+";
    case ClassOp::Sub: return "-";
    case ClassOp::Mul: return "*";
    case ClassOp::Div: return "/";
    case ClassOp::Mod: return "%";
    case ClassOp::Shl: return "<<";
    case ClassOp::Shr: return ">>";
    case ClassOp::Neg: return "-";
    }
    return "?";
}

ClassicalExpr ClassicalExpr::constant(std::int64_t v) {
    ClassicalExpr e;
    e.op = ClassOp::Const;
    e.value = v;
    return e;
}
ClassicalExpr ClassicalExpr::bitRef(ClassicalBit b) {
    ClassicalExpr e;
    e.op = ClassOp::BitRef;
    e.bit = b;
    return e;
}
ClassicalExpr ClassicalExpr::regRef(CregRef r, bool isSigned) {
    ClassicalExpr e;
    e.op = ClassOp::RegRef;
    e.reg = r;
    e.isSigned = isSigned;
    return e;
}
ClassicalExpr ClassicalExpr::unary(ClassOp op, ClassicalExpr a) {
    ClassicalExpr e;
    e.op = op;
    e.args.push_back(std::move(a));
    return e;
}
ClassicalExpr ClassicalExpr::binary(ClassOp op, ClassicalExpr a, ClassicalExpr b) {
    ClassicalExpr e;
    e.op = op;
    e.args.push_back(std::move(a));
    e.args.push_back(std::move(b));
    return e;
}

void ClassicalExpr::collectBits(std::vector<ClassicalBit>& out) const {
    if (op == ClassOp::BitRef) out.push_back(bit);
    else if (op == ClassOp::RegRef)
        for (std::uint32_t i = 0; i < reg.size; ++i) out.push_back(reg.bit(i));
    for (const auto& a : args) a.collectBits(out);
}
std::vector<ClassicalBit> ClassicalExpr::reads() const {
    std::vector<ClassicalBit> v;
    collectBits(v);
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

std::string ClassicalExpr::text() const {
    switch (op) {
    case ClassOp::Const: return std::format("{}", value);
    case ClassOp::BitRef: return std::format("c[{}]", bit.index);
    case ClassOp::RegRef:
        return std::format("{}c[{}:{}]", isSigned ? "int " : "", reg.first, reg.first + reg.size - 1);
    case ClassOp::LogicNot: case ClassOp::BitNot: case ClassOp::Neg:
        return std::format("({}{})", classOpName(op), args.empty() ? "<malformed>" : args[0].text());
    default:
        if (args.size() != 2) return "<malformed>";
        return std::format("({} {} {})", args[0].text(), classOpName(op), args[1].text());
    }
}

std::int64_t ClassicalExpr::eval(std::span<const std::uint8_t> bits) const {
    auto get = [&](std::uint32_t i) -> std::int64_t { return i < bits.size() && bits[i] != 0 ? 1 : 0; };
    switch (op) {
    case ClassOp::Const: return value;
    case ClassOp::BitRef: return get(bit.index);
    case ClassOp::RegRef: {
        const std::uint32_t n = std::min<std::uint32_t>(reg.size, 63);
        std::int64_t v = 0;
        for (std::uint32_t i = 0; i < n; ++i) v |= get(reg.first + i) << i; // little-endian
        if (isSigned && n > 0 && get(reg.first + n - 1) != 0) v -= (std::int64_t{1} << n); // two's complement
        return v;
    }
    case ClassOp::LogicNot: return !args.empty() && args[0].eval(bits) == 0 ? 1 : 0;
    case ClassOp::BitNot: return args.empty() ? 0 : ~args[0].eval(bits);
    case ClassOp::Neg: return args.empty() ? 0 : -args[0].eval(bits);
    default: break;
    }
    if (args.size() != 2) return 0;
    const std::int64_t a = args[0].eval(bits);
    if (op == ClassOp::LogicAnd) return (a != 0 && args[1].eval(bits) != 0) ? 1 : 0;
    if (op == ClassOp::LogicOr) return (a != 0 || args[1].eval(bits) != 0) ? 1 : 0;
    const std::int64_t b = args[1].eval(bits);
    const bool shiftOk = b >= 0 && b < 63;
    switch (op) {
    case ClassOp::Eq: return a == b;
    case ClassOp::Ne: return a != b;
    case ClassOp::Lt: return a < b;
    case ClassOp::Le: return a <= b;
    case ClassOp::Gt: return a > b;
    case ClassOp::Ge: return a >= b;
    case ClassOp::BitAnd: return a & b;
    case ClassOp::BitOr: return a | b;
    case ClassOp::BitXor: return a ^ b;
    case ClassOp::Add: return a + b;
    case ClassOp::Sub: return a - b;
    case ClassOp::Mul: return a * b;
    case ClassOp::Div: return b == 0 ? 0 : a / b;
    case ClassOp::Mod: return b == 0 ? 0 : a % b;
    case ClassOp::Shl: return shiftOk ? a << b : 0;
    case ClassOp::Shr: return shiftOk ? a >> b : (a < 0 ? -1 : 0);
    default: return 0;
    }
}

bool ClassicalExpr::operator==(const ClassicalExpr& o) const {
    return op == o.op && value == o.value && bit == o.bit && reg == o.reg && isSigned == o.isSigned &&
           args == o.args;
}

std::string ClassicalTarget::text() const {
    return element ? std::format("c[{}]", reg.first + *element)
                   : std::format("c[{}:{}]", reg.first, reg.first + reg.size - 1);
}

void applyClassicalOp(const ClassicalOp& op, std::span<std::uint8_t> bits) {
    // Bit-pattern assignment: the value is truncated to the width of the target (spec 13 §3).
    const auto v = static_cast<std::uint64_t>(op.expr.eval(bits));
    auto put = [&](std::uint32_t i, std::uint64_t b) {
        if (i < bits.size()) bits[i] = static_cast<std::uint8_t>(b & 1u);
    };
    if (op.dst.element) {
        put(op.dst.reg.first + *op.dst.element, v);
        return;
    }
    for (std::uint32_t i = 0; i < op.dst.reg.size; ++i) put(op.dst.reg.first + i, i < 64 ? (v >> i) : 0);
}

} // namespace qlab::ir
