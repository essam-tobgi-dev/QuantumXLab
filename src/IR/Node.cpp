// Spec 14 §3 — node helpers: nested circuits, gate matrices, wire and bit queries.
// The classical-expression half of Node.hpp lives in ClassicalExpr.cpp.
#include "IR/Circuit.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Matrix.hpp"
#include <algorithm>
#include <format>

namespace qlab::ir {

// ---------------------------------------------------------------- SubCircuit
SubCircuit::SubCircuit() = default;
SubCircuit::SubCircuit(Circuit c) : p_(std::make_unique<Circuit>(std::move(c))) {}
SubCircuit::SubCircuit(const SubCircuit& o)
    : p_(o.p_ ? std::make_unique<Circuit>(*o.p_) : nullptr) {}
SubCircuit::SubCircuit(SubCircuit&& o) noexcept = default;
SubCircuit& SubCircuit::operator=(const SubCircuit& o) {
    if (this != &o)
        p_ = o.p_ ? std::make_unique<Circuit>(*o.p_) : nullptr;
    return *this;
}
SubCircuit& SubCircuit::operator=(SubCircuit&& o) noexcept = default;
SubCircuit::~SubCircuit() = default;
Circuit& SubCircuit::operator*() {
    if (!p_)
        p_ = std::make_unique<Circuit>();
    return *p_;
}
const Circuit& SubCircuit::operator*() const {
    static const Circuit empty;
    return p_ ? *p_ : empty;
}

// ---------------------------------------------------------------- Gate
std::vector<Wire> Gate::wires() const {
    std::vector<Wire> w = targets;
    w.insert(w.end(), controls.begin(), controls.end());
    return w;
}

Result<Gate> makeGate(std::string_view name, std::vector<Wire> targets, std::vector<double> params,
                      SourceSpan span) {
    const GateDef* d = gates::find(name);
    if (!d)
        return fail(err::UnknownGate, std::format("unknown gate '{}'", name));
    if (static_cast<int>(params.size()) != d->nParams)
        return fail(err::BadArity, std::format("gate '{}' takes {} parameter(s), got {}", name,
                                               d->nParams, params.size()));
    if (static_cast<int>(targets.size()) != d->nQubits)
        return fail(err::BadArity, std::format("gate '{}' acts on {} qubit(s), got {}", name,
                                               d->nQubits, targets.size()));
    Gate g;
    g.name = std::string(name);
    g.params = std::move(params);
    g.targets = std::move(targets);
    g.cls = gates::classify(g.name, g.params);
    g.span = std::move(span);
    return g;
}

Result<Gate> makeUnitary(num::Matrix matrix, std::vector<Wire> targets, SourceSpan span) {
    if (!matrix.square() || matrix.rows == 0)
        return fail(err::BadArity, std::format("unitary matrix must be square, got {}×{}",
                                               matrix.rows, matrix.cols));
    const std::size_t want = std::size_t{1} << targets.size();
    if (matrix.rows != want)
        return fail(err::BadArity,
                    std::format("unitary on {} qubit(s) needs a {}×{} matrix, got {}×{}",
                                targets.size(), want, want, matrix.rows, matrix.cols));
    if (!num::isUnitary(matrix.view(), num::tol::kUnitaryTol))
        return fail(err::NotUnitary, "matrix of a 'unitary' node is not unitary");
    Gate g;
    g.name = "unitary";
    g.targets = std::move(targets);
    g.custom = std::move(matrix);
    g.cls = GateClass::Generic;
    g.span = std::move(span);
    return g;
}

Result<num::Matrix> baseMatrixOf(const Gate& g) {
    if (g.opaque)
        return fail(err::Unsupported,
                    std::format("gate '{}' is defined only by a defcal and has no matrix", g.name));
    num::Matrix base;
    if (g.custom) {
        base = *g.custom;
        const std::size_t want = std::size_t{1} << g.targets.size();
        if (base.rows != want || base.cols != want)
            return fail(err::BadArity,
                        std::format("gate '{}' carries a {}×{} matrix on {} target(s)", g.name,
                                    base.rows, base.cols, g.targets.size()));
    } else {
        QXL_TRY_ASSIGN(base, gates::matrix(g.name, g.params));
    }
    if (g.adjoint)
        base = num::adjoint(base.view());
    return base;
}

Result<num::Matrix> matrixOf(const Gate& g) {
    if (g.matrixCache)
        return *g.matrixCache;
    QXL_TRY_ASSIGN(num::Matrix m, baseMatrixOf(g));
    if (!g.controls.empty())
        m = gates::controlled(m.view(), g.controls.size(), g.negControl);
    g.matrixCache = m;
    return m;
}

Result<qsim::GateOp> toGateOp(const Gate& g) {
    qsim::GateOp op;
    op.name = g.name;
    bool anyNegative = false;
    for (std::size_t j = 0; j < g.controls.size(); ++j)
        anyNegative = anyNegative || g.isNegControl(j);
    if (anyNegative) {
        // A |0⟩-activated control has no backend fast path: hand over the full matrix (spec 07
        // §2.2).
        QXL_TRY_ASSIGN(op.matrix, matrixOf(g));
        for (Wire w : g.wires())
            op.targets.push_back(QubitIndex{w.index});
        op.cls = GateClass::Generic;
        return op;
    }
    QXL_TRY_ASSIGN(op.matrix, baseMatrixOf(g));
    for (Wire w : g.targets)
        op.targets.push_back(QubitIndex{w.index});
    for (Wire w : g.controls)
        op.controls.push_back(QubitIndex{w.index});
    op.cls = g.cls;
    return op;
}

// ---------------------------------------------------------------- node queries
namespace {
std::vector<Wire> sortedUnique(std::vector<Wire> w) {
    std::sort(w.begin(), w.end());
    w.erase(std::unique(w.begin(), w.end()), w.end());
    return w;
}
std::vector<Wire> wiresOfBody(const Circuit& c) {
    std::vector<Wire> w;
    for (NodeId id : c.topologicalOrder())
        for (Wire x : nodeWires(c.node(id)))
            w.push_back(x);
    return sortedUnique(std::move(w));
}
} // namespace

std::vector<Wire> nodeWires(const Node& n) {
    return std::visit(
        [](const auto& v) -> std::vector<Wire> {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Gate>)
                return v.wires();
            else if constexpr (std::is_same_v<T, Measure> || std::is_same_v<T, Reset>)
                return {v.qubit};
            else if constexpr (std::is_same_v<T, Barrier> || std::is_same_v<T, Delay>)
                return v.wires;
            else if constexpr (std::is_same_v<T, ClassicalOp>)
                return {};
            else if constexpr (std::is_same_v<T, Branch>) {
                std::vector<Wire> w = wiresOfBody(*v.thenBody);
                for (Wire x : wiresOfBody(*v.elseBody))
                    w.push_back(x);
                return sortedUnique(std::move(w));
            } else
                return wiresOfBody(*v.body);
        },
        n);
}

std::vector<Wire> nodeWiresIn(const Node& n, std::uint32_t qubitCount) {
    auto w = nodeWires(n);
    // "empty means every wire" for barrier and delay (spec 13 §3).
    const bool spansAll =
        w.empty() && (std::holds_alternative<Barrier>(n) || std::holds_alternative<Delay>(n));
    if (!spansAll)
        return w;
    w.reserve(qubitCount);
    for (std::uint32_t q = 0; q < qubitCount; ++q)
        w.push_back(Wire{q});
    return w;
}

std::vector<ClassicalBit> nodeWrites(const Node& n) {
    if (const auto* m = std::get_if<Measure>(&n))
        return m->discards() ? std::vector<ClassicalBit>{} : std::vector{m->bit};
    if (const auto* c = std::get_if<ClassicalOp>(&n)) {
        std::vector<ClassicalBit> v;
        if (c->dst.element)
            v.push_back(c->dst.reg.bit(*c->dst.element));
        else
            for (std::uint32_t i = 0; i < c->dst.reg.size; ++i)
                v.push_back(c->dst.reg.bit(i));
        return v;
    }
    auto ofBody = [](const Circuit& c, std::vector<ClassicalBit>& out) {
        for (NodeId id : c.topologicalOrder())
            for (auto x : nodeWrites(c.node(id)))
                out.push_back(x);
    };
    if (const auto* b = std::get_if<Branch>(&n)) {
        std::vector<ClassicalBit> v;
        ofBody(*b->thenBody, v);
        ofBody(*b->elseBody, v);
        return v;
    }
    if (const auto* l = std::get_if<Loop>(&n)) {
        std::vector<ClassicalBit> v;
        ofBody(*l->body, v);
        return v;
    }
    if (const auto* bx = std::get_if<Box>(&n)) {
        std::vector<ClassicalBit> v;
        ofBody(*bx->body, v);
        return v;
    }
    return {};
}

std::vector<ClassicalBit> nodeReads(const Node& n) {
    if (const auto* c = std::get_if<ClassicalOp>(&n))
        return c->expr.reads();
    std::vector<ClassicalBit> v;
    std::vector<const Circuit*> bodies;
    if (const auto* b = std::get_if<Branch>(&n)) {
        v = b->cond.reads();
        bodies = {&*b->thenBody, &*b->elseBody};
    } else if (const auto* l = std::get_if<Loop>(&n)) {
        v = l->cond.reads();
        bodies = {&*l->body};
    } else if (const auto* bx = std::get_if<Box>(&n))
        bodies = {&*bx->body};
    else
        return {};
    // A control node depends on every bit its nested circuits read (spec 14 §3 DAG edges).
    for (const Circuit* body : bodies)
        for (NodeId id : body->topologicalOrder())
            for (auto x : nodeReads(body->node(id)))
                v.push_back(x);
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

std::string_view nodeKindName(const Node& n) {
    return std::visit(
        [](const auto& v) -> std::string_view {
            using T = std::decay_t<decltype(v)>;
            (void)v;
            if constexpr (std::is_same_v<T, Gate>)
                return "gate";
            else if constexpr (std::is_same_v<T, Measure>)
                return "measure";
            else if constexpr (std::is_same_v<T, Reset>)
                return "reset";
            else if constexpr (std::is_same_v<T, Barrier>)
                return "barrier";
            else if constexpr (std::is_same_v<T, Delay>)
                return "delay";
            else if constexpr (std::is_same_v<T, ClassicalOp>)
                return "classical";
            else if constexpr (std::is_same_v<T, Branch>)
                return "branch";
            else if constexpr (std::is_same_v<T, Loop>)
                return "loop";
            else
                return "box";
        },
        n);
}

const SourceSpan& nodeSpan(const Node& n) {
    return std::visit([](const auto& v) -> const SourceSpan& { return v.span; }, n);
}

bool isQuantum(const Node& n) {
    return std::holds_alternative<Gate>(n) || std::holds_alternative<Measure>(n) ||
           std::holds_alternative<Reset>(n);
}

} // namespace qlab::ir
