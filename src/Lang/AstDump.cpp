#include "Lang/Ast.hpp"
#include <format>
#include <sstream>

namespace qlab::lang {

const char* baseTypeName(BaseType t) {
    switch (t) {
    case BaseType::Void:
        return "void";
    case BaseType::Bit:
        return "bit";
    case BaseType::Bool:
        return "bool";
    case BaseType::Int:
        return "int";
    case BaseType::Uint:
        return "uint";
    case BaseType::Float:
        return "float";
    case BaseType::Angle:
        return "angle";
    case BaseType::Duration:
        return "duration";
    case BaseType::Stretch:
        return "stretch";
    case BaseType::Complex:
        return "complex";
    case BaseType::Qubit:
        return "qubit";
    }
    return "?";
}
const char* binaryOpName(BinaryOp op) {
    switch (op) {
    case BinaryOp::Add:
        return "+";
    case BinaryOp::Sub:
        return "-";
    case BinaryOp::Mul:
        return "*";
    case BinaryOp::Div:
        return "/";
    case BinaryOp::Mod:
        return "%";
    case BinaryOp::Pow:
        return "**";
    case BinaryOp::Eq:
        return "==";
    case BinaryOp::Ne:
        return "!=";
    case BinaryOp::Lt:
        return "<";
    case BinaryOp::Le:
        return "<=";
    case BinaryOp::Gt:
        return ">";
    case BinaryOp::Ge:
        return ">=";
    case BinaryOp::And:
        return "&&";
    case BinaryOp::Or:
        return "||";
    case BinaryOp::BitAnd:
        return "&";
    case BinaryOp::BitOr:
        return "|";
    case BinaryOp::BitXor:
        return "^";
    case BinaryOp::Shl:
        return "<<";
    case BinaryOp::Shr:
        return ">>";
    }
    return "?";
}
const char* unaryOpName(UnaryOp op) {
    switch (op) {
    case UnaryOp::Neg:
        return "-";
    case UnaryOp::Not:
        return "!";
    case UnaryOp::BitNot:
        return "~";
    }
    return "?";
}

namespace {
std::string typeStr(const TypeSpec& t) {
    std::string s = baseTypeName(t.base);
    if (t.width)
        s += "[" + dumpExpr(**t.width) + "]";
    return s;
}
std::string num(double v) {
    return std::format("{:.12g}", v);
}
std::string list(const std::vector<ExprPtr>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i)
            s += " ";
        s += v[i] ? dumpExpr(*v[i]) : "<null>";
    }
    return s;
}
std::string ids(const std::vector<std::string>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i)
            s += " ";
        s += v[i];
    }
    return s;
}
std::string body(const StmtList& b, int ind) {
    std::string s;
    for (auto& st : b)
        if (st)
            s += dumpStmt(*st, ind);
    return s;
}
std::string pulse(const PulseStmt& p) {
    return std::visit(
        [&](const auto& n) -> std::string {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, PulsePortDecl>)
                return "(port " + n.name + ")";
            else if constexpr (std::is_same_v<T, PulseFrameDecl>)
                return "(frame " + n.name + " " + n.port + " " + dumpExpr(*n.frequency) + " " +
                       dumpExpr(*n.phase) + ")";
            else if constexpr (std::is_same_v<T, PulseWaveformDecl>)
                return "(waveform " + n.name + " (" + n.waveform.kind + " " +
                       list(n.waveform.args) + "))";
            else if constexpr (std::is_same_v<T, PulsePlay>) {
                if (auto* s = std::get_if<std::string>(&n.waveform))
                    return "(play " + n.frame + " " + *s + ")";
                auto& w = std::get<PulseWaveform>(n.waveform);
                return "(play " + n.frame + " (" + w.kind + " " + list(w.args) + "))";
            } else if constexpr (std::is_same_v<T, PulseFrameOp>)
                return "(" + n.op + " " + n.frame + " " + dumpExpr(*n.value) + ")";
            else if constexpr (std::is_same_v<T, PulseDelay>)
                return "(pdelay " + dumpExpr(*n.duration) + " " + ids(n.frames) + ")";
            else if constexpr (std::is_same_v<T, PulseCapture>)
                return "(capture " + n.frame + " " + dumpExpr(*n.duration) +
                       (n.target ? " -> " + *n.target : "") + ")";
            else if constexpr (std::is_same_v<T, PulseBarrier>)
                return "(pbarrier " + ids(n.frames) + ")";
            else
                return "(?)";
        },
        p.node);
}
} // namespace

std::string dumpExpr(const Expr& e) {
    return std::visit(
        [&](const auto& n) -> std::string {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, IntLit>)
                return std::to_string(n.value);
            else if constexpr (std::is_same_v<T, FloatLit>)
                return num(n.value);
            else if constexpr (std::is_same_v<T, ImagLit>)
                return num(n.value) + "im";
            else if constexpr (std::is_same_v<T, BoolLit>)
                return n.value ? "true" : "false";
            else if constexpr (std::is_same_v<T, BitStringLit>)
                return "\"" + n.bits + "\"";
            else if constexpr (std::is_same_v<T, DurationLit>)
                return num(n.value) + durationUnitName(n.unit);
            else if constexpr (std::is_same_v<T, ConstantRef>)
                return n.name;
            else if constexpr (std::is_same_v<T, Ident>)
                return n.name;
            else if constexpr (std::is_same_v<T, PhysicalQubitRef>)
                return "$" + std::to_string(n.index);
            else if constexpr (std::is_same_v<T, UnaryExpr>)
                return std::string("(") + unaryOpName(n.op) + " " + dumpExpr(*n.operand) + ")";
            else if constexpr (std::is_same_v<T, BinaryExpr>)
                return std::string("(") + binaryOpName(n.op) + " " + dumpExpr(*n.lhs) + " " +
                       dumpExpr(*n.rhs) + ")";
            else if constexpr (std::is_same_v<T, CallExpr>)
                return "(call " + n.callee + (n.args.empty() ? "" : " " + list(n.args)) + ")";
            else if constexpr (std::is_same_v<T, CastExpr>)
                return "(cast " + typeStr(n.type) + " " + dumpExpr(*n.operand) + ")";
            else if constexpr (std::is_same_v<T, IndexExpr>)
                return "(index " + dumpExpr(*n.base) + " " + dumpExpr(*n.index) + ")";
            else if constexpr (std::is_same_v<T, RangeExpr>)
                return "(range " + dumpExpr(*n.start) + (n.step ? " " + dumpExpr(**n.step) : "") +
                       " " + dumpExpr(*n.stop) + ")";
            else if constexpr (std::is_same_v<T, SliceExpr>)
                return "(slice " + dumpExpr(*n.base) + " " + dumpExpr(*n.range) + ")";
            else if constexpr (std::is_same_v<T, SetExpr>)
                return "(set " + list(n.items) + ")";
            else if constexpr (std::is_same_v<T, ConcatExpr>)
                return "(++ " + dumpExpr(*n.lhs) + " " + dumpExpr(*n.rhs) + ")";
            else if constexpr (std::is_same_v<T, MeasureExpr>)
                return "(measure " + list(n.qubits) + ")";
            else if constexpr (std::is_same_v<T, DurationOfExpr>)
                return "(durationof ...)";
            else if constexpr (std::is_same_v<T, ErrorExpr>)
                return "(error)";
            else
                return "(?)";
        },
        e.node);
}

std::string dumpStmt(const Stmt& s, int indent) {
    std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
    std::string out = std::visit(
        [&](const auto& n) -> std::string {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, VersionStmt>)
                return "(version " + n.version + ")";
            else if constexpr (std::is_same_v<T, IncludeStmt>)
                return "(include \"" + n.path + "\")";
            else if constexpr (std::is_same_v<T, QubitDecl>)
                return "(qubit " + n.name + (n.size ? " " + dumpExpr(**n.size) : "") + ")";
            else if constexpr (std::is_same_v<T, ClassicalDecl>) {
                std::string io = n.io == IoKind::Input    ? "input "
                                 : n.io == IoKind::Output ? "output "
                                 : n.io == IoKind::Const  ? "const "
                                                          : "";
                return "(decl " + io + typeStr(n.type) + " " + n.name +
                       (n.init ? " " + dumpExpr(**n.init) : "") + ")";
            } else if constexpr (std::is_same_v<T, GateDecl>)
                return "(gate " + n.name + " (" + ids(n.params) + ") (" + ids(n.qubits) + ")\n" +
                       body(n.body, indent + 1) + pad + ")";
            else if constexpr (std::is_same_v<T, GateCall>) {
                std::string m;
                for (auto& mod : n.modifiers) {
                    m += mod.kind == ModifierKind::Ctrl      ? "ctrl"
                         : mod.kind == ModifierKind::NegCtrl ? "negctrl"
                         : mod.kind == ModifierKind::Inv     ? "inv"
                                                             : "pow";
                    if (mod.arg)
                        m += "(" + dumpExpr(**mod.arg) + ")";
                    m += "@ ";
                }
                return "(" + m + n.name + (n.params.empty() ? "" : " (" + list(n.params) + ")") +
                       " " + list(n.qubits) + ")";
            } else if constexpr (std::is_same_v<T, MeasureStmt>)
                return "(measure " + list(n.qubits) +
                       (n.target ? " -> " + dumpExpr(**n.target) : "") + ")";
            else if constexpr (std::is_same_v<T, ResetStmt>)
                return "(reset " + list(n.qubits) + ")";
            else if constexpr (std::is_same_v<T, BarrierStmt>)
                return "(barrier " + list(n.qubits) + ")";
            else if constexpr (std::is_same_v<T, DelayStmt>)
                return "(delay " + dumpExpr(*n.duration) + " " + list(n.qubits) + ")";
            else if constexpr (std::is_same_v<T, BoxStmt>)
                return "(box" + std::string(n.duration ? " " + dumpExpr(**n.duration) : "") + "\n" +
                       body(n.body, indent + 1) + pad + ")";
            else if constexpr (std::is_same_v<T, AssignStmt>)
                return "(assign " + dumpExpr(*n.target) + " " + dumpExpr(*n.value) + ")";
            else if constexpr (std::is_same_v<T, ExprStmt>)
                return "(expr " + dumpExpr(*n.expr) + ")";
            else if constexpr (std::is_same_v<T, IfStmt>)
                return "(if " + dumpExpr(*n.cond) + "\n" + body(n.thenBody, indent + 1) +
                       (n.hasElse ? pad + " else\n" + body(n.elseBody, indent + 1) : "") + pad +
                       ")";
            else if constexpr (std::is_same_v<T, ForStmt>)
                return "(for " + typeStr(n.type) + " " + n.var + " " + dumpExpr(*n.iterable) +
                       "\n" + body(n.body, indent + 1) + pad + ")";
            else if constexpr (std::is_same_v<T, WhileStmt>)
                return "(while " + dumpExpr(*n.cond) + "\n" + body(n.body, indent + 1) + pad + ")";
            else if constexpr (std::is_same_v<T, SwitchStmt>)
                return "(switch " + dumpExpr(*n.subject) + ")";
            else if constexpr (std::is_same_v<T, DefStmt>) {
                std::string ps;
                for (auto& p : n.params)
                    ps += (p.isQubit ? "qubit " : typeStr(p.type) + " ") + p.name + ", ";
                return "(def " + n.name + " (" + ps + ") -> " + typeStr(n.returnType) + "\n" +
                       body(n.body, indent + 1) + pad + ")";
            } else if constexpr (std::is_same_v<T, ExternStmt>)
                return "(extern " + n.name + ")";
            else if constexpr (std::is_same_v<T, ReturnStmt>)
                return "(return" + std::string(n.value ? " " + dumpExpr(**n.value) : "") + ")";
            else if constexpr (std::is_same_v<T, BreakStmt>)
                return "(break)";
            else if constexpr (std::is_same_v<T, ContinueStmt>)
                return "(continue)";
            else if constexpr (std::is_same_v<T, EndStmt>)
                return "(end)";
            else if constexpr (std::is_same_v<T, CalStmt>) {
                std::string s = "(cal\n";
                for (auto& p : n.body)
                    s += pad + "  " + pulse(p) + "\n";
                return s + pad + ")";
            } else if constexpr (std::is_same_v<T, DefcalStmt>) {
                std::string q;
                for (auto& r : n.qubits)
                    q += "$" + std::to_string(r.index) + " ";
                std::string s = "(defcal " + n.gate + " " + q + "\n";
                for (auto& p : n.body)
                    s += pad + "  " + pulse(p) + "\n";
                return s + pad + ")";
            } else if constexpr (std::is_same_v<T, PragmaStmt>)
                return "(pragma " + std::string(n.isQlab ? "qlab." : "") + n.name + " " +
                       ids(n.args) + ")";
            else
                return "(?)";
        },
        s.node);
    return pad + out + "\n";
}

std::string dumpAst(const Ast& a) {
    std::string s = "(program \"" + a.filename + "\"\n";
    for (auto& st : a.statements)
        if (st)
            s += dumpStmt(*st, 1);
    return s + ")\n";
}

} // namespace qlab::lang
