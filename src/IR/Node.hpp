#pragma once
// Spec 14 §3 — IR nodes. A node's wires are its only ordering constraint; edges are implied.
#include "IR/Gates.hpp"
#include "IR/Types.hpp"
#include "Numerics/Types.hpp"
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace qlab::ir {

class Circuit;

// Copyable owning pointer to a nested circuit (Branch, Loop, Box). Deep-copies on copy; all
// special members are defined out of line, where `Circuit` is complete.
class SubCircuit {
  public:
    SubCircuit();
    explicit SubCircuit(Circuit c);
    SubCircuit(const SubCircuit& o);
    SubCircuit(SubCircuit&& o) noexcept;
    SubCircuit& operator=(const SubCircuit& o);
    SubCircuit& operator=(SubCircuit&& o) noexcept;
    ~SubCircuit();
    Circuit& operator*();
    const Circuit& operator*() const;
    Circuit* operator->() { return &**this; }
    const Circuit* operator->() const { return &**this; }
    bool present() const { return p_ != nullptr; }

  private:
    std::unique_ptr<Circuit> p_;
};

// ---------------------------------------------------------------- classical expressions
enum class ClassOp {
    Const,
    BitRef,
    RegRef,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    LogicAnd,
    LogicOr,
    LogicNot,
    BitAnd,
    BitOr,
    BitXor,
    BitNot,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Shl,
    Shr,
    Neg
};
const char* classOpName(ClassOp op);

// Expression over the flat classical bit space. Registers are read little-endian (Types.hpp);
// `isSigned` reads a RegRef as two's complement (an `int[w]` variable, RegKind::Int).
struct ClassicalExpr {
    ClassOp op = ClassOp::Const;
    std::int64_t value = 0; // Const
    ClassicalBit bit{};     // BitRef
    CregRef reg{};          // RegRef
    bool isSigned = false;  // RegRef
    std::vector<ClassicalExpr> args;

    static ClassicalExpr constant(std::int64_t v);
    static ClassicalExpr bitRef(ClassicalBit b);
    static ClassicalExpr regRef(CregRef r, bool isSigned = false);
    static ClassicalExpr unary(ClassOp op, ClassicalExpr a);
    static ClassicalExpr binary(ClassOp op, ClassicalExpr a, ClassicalExpr b);
    bool isConst() const { return op == ClassOp::Const; }
    void collectBits(std::vector<ClassicalBit>& out) const;
    std::vector<ClassicalBit> reads() const;
    std::string text() const; // deterministic, flat indices (dump)
    std::int64_t eval(std::span<const std::uint8_t> bits) const; // for tests and the runtime
    bool operator==(const ClassicalExpr& o) const;
};

struct ClassicalTarget {
    CregRef reg{};
    std::optional<std::uint32_t> element; // set = single bit of the register
    std::string text() const;
    bool operator==(const ClassicalTarget&) const = default;
};

// ---------------------------------------------------------------- nodes
// Canonical form produced by `buildCircuit` (spec 14 §2): user gates are expanded; `ctrl @` on a
// gate with a named controlled version is absorbed into it (ctrl @ x → cx, ctrl(2) @ x → ccx,
// ctrl @ U → cu(θ,φ,λ,0), ctrl @ gphase(γ) → p(γ) on the control); remaining controls stay in
// `controls` with `targets` holding the base gate's operands; `inv @` becomes a named inverse or
// `adjoint`; `pow(k) @` scales rotation angles, uses a named closed form (pow(2) @ s → z), or
// repeats the node |k| times. Matrices are never multiplied at build time.
struct Gate {
    std::string name;
    std::vector<double> params;
    std::vector<Wire> targets;
    std::vector<Wire> controls;
    // Empty when every control is positive; otherwise parallel to `controls`, 1 = active on |0⟩.
    std::vector<std::uint8_t> negControl;
    bool adjoint = false;              // `inv @` with no named inverse
    bool opaque = false;               // defined only by a `defcal` (spec 13 §5): no matrix
    std::optional<num::Matrix> custom; // base matrix of a "unitary" gate, 2^|targets| square
    // Lazy cache of `matrixOf` (2^k × 2^k, k = |targets| + |controls|). Whoever changes name,
    // params, controls, adjoint or custom must reset it.
    mutable std::optional<num::Matrix> matrixCache;
    std::optional<Picoseconds> duration; // set by the scheduler (spec 14 §9)
    GateClass cls = GateClass::Generic;  // class of the BASE operation on `targets`
    SourceSpan span;

    // Canonical wire order: targets first, then controls. wires()[0] is the LEAST significant
    // index of `matrixOf`, so embedding is `num::embed(matrixOf(g), indices(wires()), n)`.
    std::vector<Wire> wires() const;
    std::size_t width() const { return targets.size() + controls.size(); }
    bool isNegControl(std::size_t j) const { return j < negControl.size() && negControl[j] != 0; }
};

struct Measure {
    Wire qubit;
    ClassicalBit bit = kNoBit; // kNoBit = outcome discarded
    std::optional<Picoseconds> duration;
    SourceSpan span;
    bool discards() const { return bit == kNoBit; }
};
struct Reset {
    Wire qubit;
    std::optional<Picoseconds> duration;
    SourceSpan span;
};
struct Barrier {
    std::vector<Wire> wires;
    SourceSpan span;
}; // empty = every wire
struct Delay {
    Duration duration{};
    std::vector<Wire> wires;
    SourceSpan span;
}; // empty = every wire
struct ClassicalOp {
    ClassicalExpr expr;
    ClassicalTarget dst;
    SourceSpan span;
};
struct Branch {
    ClassicalExpr cond;
    SubCircuit thenBody;
    SubCircuit elseBody;
    SourceSpan span;
};
// A `while` whose condition depends on measurement: executed per shot (spec 15 §3, QL5020).
struct Loop {
    ClassicalExpr cond;
    SubCircuit body;
    std::uint32_t maxIterations = 0;
    SourceSpan span;
};
struct Box {
    std::optional<Duration> duration;
    SubCircuit body;
    SourceSpan span;
};

using Node = std::variant<Gate, Measure, Reset, Barrier, Delay, ClassicalOp, Branch, Loop, Box>;

// Library gate node with its arity checked and `cls` filled in.
Result<Gate> makeGate(std::string_view name, std::vector<Wire> targets,
                      std::vector<double> params = {}, SourceSpan span = {});
// "unitary" node carrying an explicit 2^|targets| matrix (checked unitary to kUnitaryTol).
Result<Gate> makeUnitary(num::Matrix matrix, std::vector<Wire> targets, SourceSpan span = {});

// Base matrix on `targets` only (adjoint applied, controls excluded).
Result<num::Matrix> baseMatrixOf(const Gate& g);
// Matrix of a gate node including its controls, in the `wires()` order. Cached on the node.
Result<num::Matrix> matrixOf(const Gate& g);
// The same gate for a simulation backend. Positive controls stay controls; a gate with a negative
// control is handed over as its full matrix on `wires()`.
Result<qsim::GateOp> toGateOp(const Gate& g);

// Wires a node constrains (for Branch/Loop/Box: every wire of the nested circuits). Barrier and
// Delay return their explicit list; `nodeWiresIn` expands "empty = every wire".
std::vector<Wire> nodeWires(const Node& n);
std::vector<Wire> nodeWiresIn(const Node& n, std::uint32_t qubitCount);
// Classical bits a node writes / reads (nested circuits included).
std::vector<ClassicalBit> nodeWrites(const Node& n);
std::vector<ClassicalBit> nodeReads(const Node& n);
std::string_view nodeKindName(const Node& n);
const SourceSpan& nodeSpan(const Node& n);
bool isQuantum(const Node& n); // Gate, Measure, Reset (occupies a qubit for scheduling)
// Executes `op` on one shot's classical memory: the value is truncated to the target width.
void applyClassicalOp(const ClassicalOp& op, std::span<std::uint8_t> bits);

} // namespace qlab::ir
