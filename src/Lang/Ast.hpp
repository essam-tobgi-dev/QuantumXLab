#pragma once
// Spec 13 §3 — abstract syntax tree for the OpenQASM 3 + OpenPulse subset.
// Nodes are value types held in std::unique_ptr; the tree is owned by lang::Program.
#include "Core/Error.hpp"
#include "Lang/Token.hpp"
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace qlab::lang {

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using StmtList = std::vector<StmtPtr>;

// ---------------------------------------------------------------- types
enum class BaseType { Void, Bit, Bool, Int, Uint, Float, Angle, Duration, Stretch, Complex, Qubit };
struct TypeSpec {
    BaseType base = BaseType::Void;
    std::optional<ExprPtr> width; // bit[n], int[32], qubit[5] ...; absent = default/scalar
    bool isArray() const { return width.has_value(); }
};
const char* baseTypeName(BaseType t);

// ---------------------------------------------------------------- expressions
enum class UnaryOp { Neg, Not, BitNot };
enum class BinaryOp { Add, Sub, Mul, Div, Mod, Pow, Eq, Ne, Lt, Le, Gt, Ge, And, Or, BitAnd, BitOr, BitXor, Shl, Shr };
const char* binaryOpName(BinaryOp op);
const char* unaryOpName(UnaryOp op);

struct IntLit { std::int64_t value; };
struct FloatLit { double value; };
struct ImagLit { double value; };
struct BoolLit { bool value; };
struct BitStringLit { std::string bits; };
struct DurationLit { double value; DurationUnit unit; };
struct ConstantRef { std::string name; }; // pi, euler, tau
struct Ident { std::string name; };
struct PhysicalQubitRef { std::uint32_t index; };
struct UnaryExpr { UnaryOp op; ExprPtr operand; };
struct BinaryExpr { BinaryOp op; ExprPtr lhs, rhs; };
struct CallExpr { std::string callee; std::vector<ExprPtr> args; }; // sin(x), sizeof(a), user def
struct CastExpr { TypeSpec type; ExprPtr operand; };
struct IndexExpr { ExprPtr base; ExprPtr index; };
struct RangeExpr { ExprPtr start; std::optional<ExprPtr> step; ExprPtr stop; }; // [a:b] or [a:s:b]
struct SliceExpr { ExprPtr base; ExprPtr range; };
struct SetExpr { std::vector<ExprPtr> items; }; // {1, 2, 3}
struct ConcatExpr { ExprPtr lhs, rhs; };       // a ++ b
struct MeasureExpr { std::vector<ExprPtr> qubits; }; // measure q  (as an expression)
struct DurationOfExpr { StmtList body; };
// Placeholder produced when an expression fails to parse. It carries no type and is silently
// accepted by Sema so that one syntax error does not cascade into type errors (spec 13 §8).
struct ErrorExpr {};

struct Expr {
    std::variant<IntLit, FloatLit, ImagLit, BoolLit, BitStringLit, DurationLit, ConstantRef, Ident,
                 PhysicalQubitRef, UnaryExpr, BinaryExpr, CallExpr, CastExpr, IndexExpr, RangeExpr,
                 SliceExpr, SetExpr, ConcatExpr, MeasureExpr, DurationOfExpr, ErrorExpr>
        node;
    SourceSpan span;
    template <class T> const T* as() const { return std::get_if<T>(&node); }
    template <class T> T* as() { return std::get_if<T>(&node); }
    template <class T> bool is() const { return std::holds_alternative<T>(node); }
};

// ---------------------------------------------------------------- gate modifiers
enum class ModifierKind { Ctrl, NegCtrl, Inv, Pow };
struct GateModifier {
    ModifierKind kind;
    std::optional<ExprPtr> arg; // ctrl(n) count or pow(k) exponent
};

// ---------------------------------------------------------------- statements
struct VersionStmt { std::string version; };
struct IncludeStmt { std::string path; };
struct QubitDecl { std::string name; std::optional<ExprPtr> size; };
enum class IoKind { None, Input, Output, Const };
struct ClassicalDecl { TypeSpec type; std::string name; std::optional<ExprPtr> init; IoKind io = IoKind::None; };
struct GateDecl { std::string name; std::vector<std::string> params; std::vector<std::string> qubits; StmtList body; };
struct GateCall {
    std::vector<GateModifier> modifiers;
    std::string name;
    std::vector<ExprPtr> params;
    std::vector<ExprPtr> qubits;
    std::optional<ExprPtr> duration; // `delay[d]`-style designator on box/delay, unused for gates
};
struct MeasureStmt { std::vector<ExprPtr> qubits; std::optional<ExprPtr> target; }; // measure q -> c;  c = measure q;
struct ResetStmt { std::vector<ExprPtr> qubits; };
struct BarrierStmt { std::vector<ExprPtr> qubits; };
struct DelayStmt { ExprPtr duration; std::vector<ExprPtr> qubits; };
struct BoxStmt { std::optional<ExprPtr> duration; StmtList body; };
enum class AssignOp { Set, Add, Sub, Mul, Div, Mod, Pow, BitAnd, BitOr, BitXor, Shl, Shr };
struct AssignStmt { ExprPtr target; AssignOp op; ExprPtr value; };
struct ExprStmt { ExprPtr expr; };
struct IfStmt { ExprPtr cond; StmtList thenBody; StmtList elseBody; bool hasElse = false; };
struct ForStmt { TypeSpec type; std::string var; ExprPtr iterable; StmtList body; };
struct WhileStmt { ExprPtr cond; StmtList body; };
struct SwitchStmt { ExprPtr subject; }; // parsed for the diagnostic only (QL3095)
struct DefParam { TypeSpec type; std::string name; bool isQubit = false; std::optional<ExprPtr> qubitSize; };
struct DefStmt { std::string name; std::vector<DefParam> params; TypeSpec returnType; StmtList body; };
struct ExternStmt { std::string name; };
struct ReturnStmt { std::optional<ExprPtr> value; };
struct BreakStmt {};
struct ContinueStmt {};
struct EndStmt {};

// OpenPulse ----------------------------------------------------------
struct PulseWaveform { std::string kind; std::vector<ExprPtr> args; }; // gaussian(amp, dur, sigma) | constant | drag | gaussian_square
struct PulsePortDecl { std::string name; };
struct PulseFrameDecl { std::string name; std::string port; ExprPtr frequency; ExprPtr phase; };
struct PulseWaveformDecl { std::string name; PulseWaveform waveform; };
struct PulsePlay { std::string frame; std::variant<std::string, PulseWaveform> waveform; };
struct PulseFrameOp { std::string op; std::string frame; ExprPtr value; }; // set_frequency/shift_frequency/set_phase/shift_phase
struct PulseDelay { ExprPtr duration; std::vector<std::string> frames; };
struct PulseCapture { std::string frame; ExprPtr duration; std::optional<std::string> target; };
struct PulseBarrier { std::vector<std::string> frames; };
struct PulseStmt {
    std::variant<PulsePortDecl, PulseFrameDecl, PulseWaveformDecl, PulsePlay, PulseFrameOp, PulseDelay, PulseCapture, PulseBarrier> node;
    SourceSpan span;
};
struct CalStmt { std::vector<PulseStmt> body; };
struct DefcalStmt {
    std::string gate;                       // x, measure, reset, ...
    std::vector<ExprPtr> params;            // literal or symbolic params
    std::vector<std::string> paramNames;    // symbolic parameter names (angle theta)
    std::vector<PhysicalQubitRef> qubits;   // $0, $1
    TypeSpec returnType;                    // bit for measure
    std::vector<PulseStmt> body;
};

// pragma qlab.* ------------------------------------------------------
struct PragmaStmt { std::string raw; std::string name; std::vector<std::string> args; bool isQlab = false; };

struct Stmt {
    std::variant<VersionStmt, IncludeStmt, QubitDecl, ClassicalDecl, GateDecl, GateCall, MeasureStmt,
                 ResetStmt, BarrierStmt, DelayStmt, BoxStmt, AssignStmt, ExprStmt, IfStmt, ForStmt,
                 WhileStmt, SwitchStmt, DefStmt, ExternStmt, ReturnStmt, BreakStmt, ContinueStmt,
                 EndStmt, CalStmt, DefcalStmt, PragmaStmt>
        node;
    SourceSpan span;
    template <class T> const T* as() const { return std::get_if<T>(&node); }
    template <class T> T* as() { return std::get_if<T>(&node); }
    template <class T> bool is() const { return std::holds_alternative<T>(node); }
};

struct Ast {
    std::string filename;
    StmtList statements;
};

// Deterministic S-expression dump used by conformance tests (spec 13 §9).
std::string dumpExpr(const Expr& e);
std::string dumpStmt(const Stmt& s, int indent = 0);
std::string dumpAst(const Ast& a);

} // namespace qlab::lang
