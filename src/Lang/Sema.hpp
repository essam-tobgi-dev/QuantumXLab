#pragma once
// Spec 13 §3, §5, §8 — semantic analysis: scopes, types, constant folding, structure rules.
#include "Lang/Ast.hpp"
#include "Lang/Diagnostics.hpp"
#include "Lang/Parser.hpp"
#include "Lang/Pragma.hpp"
#include "Lang/StdGates.hpp"
#include <map>
#include <set>
#include <unordered_map>

namespace qlab::lang {

// Compile-time constant (spec 13 §3 "Constant evaluation").
struct ConstValue {
    enum Kind { Int, Float, Bool, Bits, DurationPs, DurationDt } kind = Int;
    std::int64_t i = 0;     // Int, Bool (0/1), DurationPs (ps), DurationDt (dt count)
    double f = 0.0;         // Float; also set for Int for convenience
    std::string bits;       // Bits
    double asDouble() const { return kind == Float ? f : static_cast<double>(i); }
    bool isNumeric() const { return kind == Int || kind == Float || kind == Bool; }
};

struct SemaType {
    BaseType base = BaseType::Void;
    int width = 0;            // bit width for int/uint/float/angle; 0 = default
    bool isRegister = false;  // bit[n] / qubit[n]
    std::size_t size = 1;     // register length
    bool operator==(const SemaType&) const = default;
};
std::string typeName(const SemaType& t);

enum class SymbolKind { QubitReg, Classical, Const, Input, Output, Gate, Def, LoopVar, GateParam, GateQubit, DefParam, Frame, Port, Waveform };

struct Symbol {
    std::string name;
    SymbolKind kind = SymbolKind::Classical;
    SemaType type;
    std::optional<ConstValue> value; // for Const / folded Input defaults / loop vars during unroll (not here)
    SourceSpan span;
    int nParams = 0, nQubits = 0;    // Gate / Def
    bool isGlobal = false;
    bool hasSweep = false;           // Input
    std::uint32_t firstQubit = 0;    // QubitReg: index of element 0 in the flat virtual-qubit space
};

struct QubitRegInfo { std::string name; std::uint32_t first; std::uint32_t size; SourceSpan span; };
struct BitRegInfo { std::string name; std::uint32_t size; SourceSpan span; };
struct InputVar { std::string name; SemaType type; std::optional<ConstValue> defaultValue; bool hasSweep; SourceSpan span; };

struct Program {
    Ast ast;
    std::vector<Diagnostic> diagnostics;
    Pragmas pragmas;
    std::vector<QubitRegInfo> qubitRegs;
    std::vector<BitRegInfo> bitRegs;
    std::vector<InputVar> inputs;
    std::vector<std::string> outputs;
    std::map<std::string, Symbol> globals;       // every global symbol
    std::map<std::string, const GateDecl*> userGates;
    std::map<std::string, const DefStmt*> defs;
    std::vector<const DefcalStmt*> defcals;
    std::uint32_t qubitCount = 0;                // total virtual qubits
    std::uint32_t bitCount = 0;
    bool usesPhysicalQubits = false;
    bool hasQuantumStatements = false;
    bool hasMidCircuitMeasurement = false;       // measurement followed by more quantum ops or feedforward
    bool hasFeedforward = false;                 // classical control depending on measurement
    bool includesStdGates = false;
    std::set<std::string> usedGates;
    std::set<std::string> pulseOnlyGates;  // called gates defined only by a `defcal` (spec 13 §5)
    bool ok() const { return !hasErrors(diagnostics); }
    std::vector<Error> errors() const { return errorsOf(diagnostics); }
    // Lookup helpers for the IR builder
    const Symbol* symbol(std::string_view name) const;
    std::optional<std::uint32_t> qubitIndex(std::string_view reg, std::size_t element) const;
};

class Sema {
public:
    explicit Sema(Program& p) : p_(p) {}
    void run();

    // Constant folding on an expression in the current scope; nullopt if not foldable.
    std::optional<ConstValue> fold(const Expr& e);
    // Static type of an expression (best effort; Void when unknown).
    SemaType typeOf(const Expr& e);
    // Null-tolerant wrappers. A well-formed AST never holds a null child (the parser emits
    // ErrorExpr instead), but a malformed one must still produce diagnostics, not a crash.
    SemaType typeOf(const ExprPtr& e) { return e ? typeOf(*e) : SemaType{}; }

private:
    struct Scope { std::map<std::string, Symbol> syms; bool isBlock = false; bool isGateBody = false; bool isDef = false; };
    template <class... A> void diag(std::string_view id, SourceSpan sp, A&&... a) { p_.diagnostics.push_back(Diagnostics::make(id, std::move(sp), std::forward<A>(a)...)); }
    void pushScope(bool block = true, bool gate = false, bool def = false) { scopes_.push_back(Scope{{}, block, gate, def}); }
    void popScope() { scopes_.pop_back(); }
    const Symbol* lookup(std::string_view name) const;
    Symbol* lookupMut(std::string_view name);
    bool declare(Symbol s);   // reports QL3011 / QL3012

    void visitStmt(const Stmt& s);
    void visitBody(const StmtList& b);
    void visitDecl(const ClassicalDecl& d, const SourceSpan& sp);
    void visitQubitDecl(const QubitDecl& d, const SourceSpan& sp);
    void visitGateDecl(const GateDecl& g, const SourceSpan& sp);
    void visitDef(const DefStmt& d, const SourceSpan& sp);
    void visitGateCall(const GateCall& c, const SourceSpan& sp);
    void visitMeasure(const MeasureStmt& m, const SourceSpan& sp);
    void visitAssign(const AssignStmt& a, const SourceSpan& sp);
    void visitIf(const IfStmt& s, const SourceSpan& sp);
    void visitFor(const ForStmt& f, const SourceSpan& sp);
    void visitWhile(const WhileStmt& w, const SourceSpan& sp);
    void visitDefcal(const DefcalStmt& d, const SourceSpan& sp);
    void visitCalBody(const std::vector<PulseStmt>& body, std::set<std::string>& frames);
    void checkExpr(const Expr& e);
    void checkExpr(const ExprPtr& e) { if (e) checkExpr(*e); }
    // Operand checks: returns the number of qubits addressed (register size or 1), 0 on error.
    std::size_t checkQubitOperand(const Expr& e, std::vector<std::pair<std::string, std::int64_t>>& seen);
    bool dependsOnMeasurement(const Expr& e) const;
    // True when the value is fixed at compile time: literals, consts, and enclosing loop
    // variables (the compiler unrolls loops, spec 14 §3), but not runtime classical state.
    bool isCompileTimeKnown(const Expr& e) const;
    void noteQuantum(const SourceSpan& sp);
    void checkRecursion();

    Program& p_;
    std::vector<Scope> scopes_;
    bool sawQuantum_ = false;
    bool sawMeasure_ = false;
    int loopDepth_ = 0;
    int defDepth_ = 0;
    std::set<std::string> measuredBits_;
    std::map<std::string, std::vector<std::string>> gateCalls_; // gate/def -> callees (recursion check)
    // Gates that exist only as a pulse-level calibration (spec 13 §5): collected before the main
    // pass because a `defcal` may legally follow its first use (QL3110).
    std::map<std::string, const DefcalStmt*> defcalGates_;
    std::string currentCallable_;
    std::set<std::string> measuringDefs_;   // defs whose body measures (feedforward detection)
    std::uint32_t nextQubit_ = 0;
};

// Entry points (spec 13 §11 / spec 14 §2). analyzeProgram never fails; parseProgram fails on the
// first error diagnostic (all diagnostics stay in the returned Program on success).
Program analyzeProgram(std::string_view text, std::string filename = "");
Result<Program> parseProgram(std::string_view text, std::string filename = "");

} // namespace qlab::lang
