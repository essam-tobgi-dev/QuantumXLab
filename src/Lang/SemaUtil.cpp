#include "Lang/SemaUtil.hpp"
#include <cmath>
#include <format>
#include <limits>
#include <type_traits>

namespace qlab::lang::detail {

bool containsErrorExpr(const Expr& e) {
    auto any = [](const std::vector<ExprPtr>& v) {
        for (auto& x : v) if (x && containsErrorExpr(*x)) return true;
        return false;
    };
    auto one = [](const ExprPtr& x) { return x && containsErrorExpr(*x); };
    return std::visit([&](const auto& n) -> bool {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, ErrorExpr>) return true;
        else if constexpr (std::is_same_v<T, UnaryExpr>) return one(n.operand);
        else if constexpr (std::is_same_v<T, BinaryExpr> || std::is_same_v<T, ConcatExpr>) return one(n.lhs) || one(n.rhs);
        else if constexpr (std::is_same_v<T, CallExpr>) return any(n.args);
        else if constexpr (std::is_same_v<T, CastExpr>) return (n.type.width && one(*n.type.width)) || one(n.operand);
        else if constexpr (std::is_same_v<T, IndexExpr>) return one(n.base) || one(n.index);
        else if constexpr (std::is_same_v<T, RangeExpr>) return one(n.start) || (n.step && one(*n.step)) || one(n.stop);
        else if constexpr (std::is_same_v<T, SliceExpr>) return one(n.base) || one(n.range);
        else if constexpr (std::is_same_v<T, SetExpr>) return any(n.items);
        else if constexpr (std::is_same_v<T, MeasureExpr>) return any(n.qubits);
        else return false;
    }, e.node);
}

std::string exprText(const Expr& e) {
    auto text = [](const ExprPtr& x) { return x ? exprText(*x) : std::string("?"); };
    return std::visit([&](const auto& n) -> std::string {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, IntLit>) return std::to_string(n.value);
        else if constexpr (std::is_same_v<T, FloatLit>) return std::format("{:.12g}", n.value);
        else if constexpr (std::is_same_v<T, BoolLit>) return n.value ? "true" : "false";
        else if constexpr (std::is_same_v<T, Ident>) return n.name;
        else if constexpr (std::is_same_v<T, ConstantRef>) return n.name;
        else if constexpr (std::is_same_v<T, PhysicalQubitRef>) return "$" + std::to_string(n.index);
        else if constexpr (std::is_same_v<T, UnaryExpr>) return std::string(unaryOpName(n.op)) + text(n.operand);
        else if constexpr (std::is_same_v<T, BinaryExpr>) return text(n.lhs) + " " + binaryOpName(n.op) + " " + text(n.rhs);
        else if constexpr (std::is_same_v<T, IndexExpr>) return text(n.base) + "[" + text(n.index) + "]";
        else if constexpr (std::is_same_v<T, RangeExpr>) return text(n.start) + ":" + (n.step ? text(*n.step) + ":" : "") + text(n.stop);
        else if constexpr (std::is_same_v<T, SliceExpr>) return text(n.base) + "[" + text(n.range) + "]";
        else if constexpr (std::is_same_v<T, MeasureExpr>) {
            std::string s = "measure";
            for (std::size_t i = 0; i < n.qubits.size(); ++i) s += (i ? ", " : " ") + text(n.qubits[i]);
            return s;
        }
        else return dumpExpr(e);
    }, e.node);
}

const char* stmtKindName(const Stmt& s) {
    return std::visit([](const auto& n) -> const char* {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, VersionStmt>) return "OPENQASM header";
        else if constexpr (std::is_same_v<T, IncludeStmt>) return "include";
        else if constexpr (std::is_same_v<T, QubitDecl>) return "qubit declaration";
        else if constexpr (std::is_same_v<T, ClassicalDecl>) return "classical declaration";
        else if constexpr (std::is_same_v<T, GateDecl>) return "gate definition";
        else if constexpr (std::is_same_v<T, GateCall>) return "gate call";
        else if constexpr (std::is_same_v<T, MeasureStmt>) return "measure";
        else if constexpr (std::is_same_v<T, ResetStmt>) return "reset";
        else if constexpr (std::is_same_v<T, BarrierStmt>) return "barrier";
        else if constexpr (std::is_same_v<T, DelayStmt>) return "delay";
        else if constexpr (std::is_same_v<T, BoxStmt>) return "box";
        else if constexpr (std::is_same_v<T, AssignStmt>) return "assignment";
        else if constexpr (std::is_same_v<T, ExprStmt>) return "expression statement";
        else if constexpr (std::is_same_v<T, IfStmt>) return "if";
        else if constexpr (std::is_same_v<T, ForStmt>) return "for";
        else if constexpr (std::is_same_v<T, WhileStmt>) return "while";
        else if constexpr (std::is_same_v<T, SwitchStmt>) return "switch";
        else if constexpr (std::is_same_v<T, DefStmt>) return "subroutine definition";
        else if constexpr (std::is_same_v<T, ExternStmt>) return "extern";
        else if constexpr (std::is_same_v<T, ReturnStmt>) return "return";
        else if constexpr (std::is_same_v<T, BreakStmt>) return "break";
        else if constexpr (std::is_same_v<T, ContinueStmt>) return "continue";
        else if constexpr (std::is_same_v<T, EndStmt>) return "end";
        else if constexpr (std::is_same_v<T, CalStmt>) return "cal block";
        else if constexpr (std::is_same_v<T, DefcalStmt>) return "defcal";
        else if constexpr (std::is_same_v<T, PragmaStmt>) return "pragma";
        else return "statement";
    }, s.node);
}

std::int64_t saturatingToInt(double v) {
    if (std::isnan(v)) return 0;
    // 2^63 is exactly representable; every double below it truncates into range.
    constexpr double kTwo63 = 9223372036854775808.0;
    if (v >= kTwo63) return std::numeric_limits<std::int64_t>::max();
    if (v < -kTwo63) return std::numeric_limits<std::int64_t>::min();
    return static_cast<std::int64_t>(v);
}

} // namespace qlab::lang::detail
