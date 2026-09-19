#pragma once
// Spec 14 §2 — internal state of the AST → IR lowering. Not part of the module API.
#include "IR/Build.hpp"
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace qlab::ir::build {

using lang::Expr;
using lang::Stmt;
using lang::StmtList;

// A build-time value: folded to a constant where it can be, otherwise an expression over the
// circuit's flat classical bit space (spec 14 §2 "Measurement and classical flow").
struct Value {
    enum class Kind { Int, Float, Dur, Symbolic };
    Kind kind = Kind::Int;
    std::int64_t i = 0;
    double f = 0.0;
    ir::Duration dur{};
    ClassicalExpr sym{};

    bool constant() const { return kind != Kind::Symbolic; }
    double asDouble() const { return kind == Kind::Float ? f : static_cast<double>(i); }
    std::int64_t asInt() const {
        return kind == Kind::Float ? static_cast<std::int64_t>(f < 0 ? f - 0.5 : f + 0.5) : i;
    }
    static Value ofInt(std::int64_t v) {
        Value x;
        x.kind = Kind::Int;
        x.i = v;
        x.f = static_cast<double>(v);
        return x;
    }
    static Value ofFloat(double v) {
        Value x;
        x.kind = Kind::Float;
        x.f = v;
        x.i = static_cast<std::int64_t>(v);
        return x;
    }
    static Value ofDuration(ir::Duration d) {
        Value x;
        x.kind = Kind::Dur;
        x.dur = d;
        return x;
    }
    static Value ofSym(ClassicalExpr e) {
        Value x;
        x.kind = Kind::Symbolic;
        x.sym = std::move(e);
        return x;
    }
    // Constants become literal ClassicalExpr leaves when they meet a symbolic operand.
    ClassicalExpr toExpr() const {
        return kind == Kind::Symbolic
                   ? sym
                   : ClassicalExpr::constant(kind == Kind::Dur ? dur.ps.get() : asInt());
    }
};

// What a name denotes while lowering.
struct Binding {
    enum class Kind { Value, Storage, Qubits };
    Kind kind = Kind::Value;
    Value value;   // Value: a compile-time constant (or a captured per-shot expression)
    CregRef reg{}; // Storage: a span of the flat classical bit space
    RegKind regKind = RegKind::Bit;
    std::vector<Wire> qubits; // Qubits: a qubit register, formal gate qubit or def qubit param
};

enum class Flow { Normal, Break, Continue, Return, End };

// Resolved modifiers of one gate application. Spec 13 §3: modifiers compose left to right and
// each `ctrl(n)`/`negctrl(n)` consumes the next n operands; controls commute with `inv`/`pow`, so a
// net exponent suffices.
struct Applied {
    std::vector<Wire> controls;           // outermost modifier first
    std::vector<std::uint8_t> negControl; // parallel to `controls`
    double power = 1.0;                   // Π pow(k) · (−1)^{#inv}
};

// A QL4xxx compile diagnostic (spec 14 §11): the Lang catalogue supplies the message, the error
// code (`ErrorCode::Compiler_ + n`) and `Error::diagnosticId`.
template <class... A> Error diagnostic(std::string_view id, const SourceSpan& sp, A&&... args) {
    return lang::Diagnostics::make(id, sp, std::forward<A>(args)...).error;
}

// Names whose value is only known per shot (BuildScan.cpp): bit variables, names assigned from a
// measurement or from another such name, and names assigned under a run-time condition.
std::set<std::string> runtimeNames(const lang::Program& p);
// Constant folding shared by expressions and compound assignment (BuildExpr.cpp).
Result<Value> foldBinary(lang::BinaryOp op, const Value& a, const Value& b);

class Builder {
  public:
    Builder(const lang::Program& p, const ParamMap& inputs, const BuildOptions& opts);
    Result<Circuit> run();

  private:
    // ---- scopes, storage, spans (Build.cpp)
    struct Found {
        Binding* binding = nullptr;
        std::size_t scope = 0;
    };
    Found find(std::string_view name);
    Binding* lookup(std::string_view name) { return find(name).binding; }
    void push() { scopes_.emplace_back(); }
    void pop() { scopes_.pop_back(); }
    void bind(const std::string& name, Binding b) {
        scopes_.back().insert_or_assign(name, std::move(b));
    }
    // Allocates a register of fresh bits. Global declarations keep their name; block-local and
    // temporary storage gets a name no global symbol or earlier register uses.
    CregRef allocRegister(std::string_view name, std::uint32_t width, RegKind kind, bool scalar,
                          bool global);
    std::string uniqueRegisterName(std::string_view base) const;
    Status setupRegisters();
    Circuit& out() { return *stack_.back(); }
    NodeId emit(Node n); // stamps the current statement span on the node
    Status countUnrolled(const SourceSpan& sp);
    std::optional<std::string> literalOf(std::string_view globalName) const;

    // ---- statements (Build.cpp dispatch, BuildStmt.cpp declarations, BuildFlow.cpp control flow)
    Status lowerBody(const StmtList& body);
    Status lowerStmt(const Stmt& s);
    Status lowerStmtKind(const Stmt& s);
    Result<Value> inputValue(const lang::ClassicalDecl& d);
    Status lowerDecl(const lang::ClassicalDecl& d);
    Status lowerAssign(const lang::AssignStmt& a);
    Status lowerMeasureInto(const std::vector<lang::ExprPtr>& qubits, const Expr* target);
    Status lowerIf(const lang::IfStmt& s);
    Status lowerFor(const lang::ForStmt& f);
    Status lowerWhile(const lang::WhileStmt& w);
    Status lowerBox(const lang::BoxStmt& b);
    // Lowers a nested block into its own Circuit (Branch arms, Loop and Box bodies).
    Result<Circuit> subCircuit(const StmtList& body, bool conditional);

    // ---- expressions (BuildExpr.cpp) and operands (BuildPlace.cpp)
    Result<Value> eval(const Expr& e);
    Result<double> evalDouble(const Expr& e);
    Result<Duration> evalDuration(const Expr& e);
    Result<Value> measureValue(const lang::MeasureExpr& m);
    // Wires named by a qubit operand: a register yields all of its wires, in little-endian order.
    Result<std::vector<Wire>> qubitOperand(const Expr& e);
    // The storage a classical assignment names, plus the element when it is indexed.
    struct Place {
        CregRef reg;
        std::optional<std::uint32_t> element;
        Binding* binding = nullptr;
        std::size_t scope = 0;
    };
    Result<Place> place(const Expr& e);
    // Inclusive OpenQASM range [start : step : stop] (spec 13 §3) without materialising it.
    struct Range {
        std::int64_t start = 0, step = 1;
        std::uint64_t count = 0;
    };
    Result<Range> range(const lang::RangeExpr& r);

    // ---- gates and subroutines (BuildGate.cpp)
    Status lowerGateCall(const lang::GateCall& c, const Applied& outer);
    Status applyGate(std::string_view name, const std::vector<double>& params,
                     const std::vector<Wire>& targets, const Applied& mod, bool opaque);
    Status emitGate(Gate g, const Applied& mod);
    Status expandUserGate(const lang::GateDecl& g, const std::vector<double>& params,
                          const std::vector<Wire>& qubits, const Applied& mod);
    Result<Value> callDef(const lang::DefStmt& d, std::span<const lang::ExprPtr> args);

    const lang::Program& p_;
    const ParamMap& inputs_;
    BuildOptions opts_;
    Circuit circuit_;
    std::vector<Circuit*> stack_;
    std::vector<std::map<std::string, Binding, std::less<>>> scopes_;
    std::set<std::string> runtime_;
    std::uint32_t nextBit_ = 0;
    std::uint64_t unrolled_ = 0;
    std::uint32_t inlineDepth_ = 0;
    int loopDepth_ = 0;
    // Index of the innermost run-time Branch/Loop body scope; bindings in lower scopes live outside
    // it and must not be changed at build time from inside (0 = not inside one).
    std::size_t conditionScope_ = 0;
    SourceSpan stmtSpan_; // span stamped on emitted nodes
    int spanPin_ = 0;     // > 0 while expanding a gate or def call: keep the call-site span
    Flow flow_ = Flow::Normal;
    std::optional<Value> returned_;
};

} // namespace qlab::ir::build
