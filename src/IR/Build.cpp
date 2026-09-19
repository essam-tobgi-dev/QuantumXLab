// Spec 14 §2 — AST → IR lowering: driver, scopes, classical storage, statement dispatch.
#include "IR/BuildImpl.hpp"
#include "IR/Emit.hpp"
#include <algorithm>
#include <format>

namespace qlab::ir::build {
namespace {
// Largest `$k` used anywhere in the program: a physical program declares no qubit registers.
std::uint32_t maxPhysical(const StmtList& b);
std::uint32_t maxPhysical(const Expr& e) {
    std::uint32_t m = 0;
    auto sub = [&](const lang::ExprPtr& x) {
        if (x)
            m = std::max(m, maxPhysical(*x));
    };
    if (const auto* n = e.as<lang::PhysicalQubitRef>())
        return n->index + 1;
    if (const auto* n = e.as<lang::MeasureExpr>())
        for (const auto& q : n->qubits)
            sub(q);
    if (const auto* n = e.as<lang::UnaryExpr>())
        sub(n->operand);
    if (const auto* n = e.as<lang::BinaryExpr>()) {
        sub(n->lhs);
        sub(n->rhs);
    }
    if (const auto* n = e.as<lang::CallExpr>())
        for (const auto& a : n->args)
            sub(a);
    if (const auto* n = e.as<lang::IndexExpr>())
        sub(n->base);
    if (const auto* n = e.as<lang::SliceExpr>())
        sub(n->base);
    return m;
}
std::uint32_t maxPhysical(const Stmt& s) {
    std::uint32_t m = 0;
    auto one = [&](const lang::ExprPtr& e) {
        if (e)
            m = std::max(m, maxPhysical(*e));
    };
    auto many = [&](const std::vector<lang::ExprPtr>& v) {
        for (const auto& e : v)
            one(e);
    };
    auto body = [&](const StmtList& b) { m = std::max(m, maxPhysical(b)); };
    if (const auto* n = s.as<lang::GateCall>()) {
        many(n->params);
        many(n->qubits);
    }
    if (const auto* n = s.as<lang::MeasureStmt>()) {
        many(n->qubits);
        if (n->target)
            one(*n->target);
    }
    if (const auto* n = s.as<lang::ResetStmt>())
        many(n->qubits);
    if (const auto* n = s.as<lang::BarrierStmt>())
        many(n->qubits);
    if (const auto* n = s.as<lang::DelayStmt>())
        many(n->qubits);
    if (const auto* n = s.as<lang::AssignStmt>()) {
        one(n->target);
        one(n->value);
    }
    if (const auto* n = s.as<lang::ExprStmt>())
        one(n->expr);
    if (const auto* n = s.as<lang::ReturnStmt>()) {
        if (n->value)
            one(*n->value);
    }
    if (const auto* n = s.as<lang::ClassicalDecl>()) {
        if (n->init)
            one(*n->init);
    }
    if (const auto* n = s.as<lang::IfStmt>()) {
        one(n->cond);
        body(n->thenBody);
        body(n->elseBody);
    }
    if (const auto* n = s.as<lang::ForStmt>())
        body(n->body);
    if (const auto* n = s.as<lang::WhileStmt>()) {
        one(n->cond);
        body(n->body);
    }
    if (const auto* n = s.as<lang::BoxStmt>())
        body(n->body);
    if (const auto* n = s.as<lang::DefStmt>())
        body(n->body);
    return m;
}
std::uint32_t maxPhysical(const StmtList& b) {
    std::uint32_t m = 0;
    for (const auto& s : b)
        if (s)
            m = std::max(m, maxPhysical(*s));
    return m;
}
// Nested bodies are created while the bit space is still growing; give them the final shape.
// `SubCircuit::operator*` creates the body on first dereference, so an absent `else` must be left
// alone: materialising it produced an empty virtual body inside a physical program, which
// `ir::verify` then rejected (spec 14 §3, body-shape rule).
void syncCounts(Circuit& c, std::uint32_t nq, std::uint32_t nc, bool physical) {
    c.setQubitCount(nq);
    c.setClbitCount(nc);
    c.setPhysical(physical);
    for (NodeId id : c.topologicalOrder()) {
        Node& n = c.node(id);
        if (auto* b = std::get_if<Branch>(&n)) {
            if (b->thenBody.present())
                syncCounts(*b->thenBody, nq, nc, physical);
            if (b->elseBody.present())
                syncCounts(*b->elseBody, nq, nc, physical);
        }
        if (auto* l = std::get_if<Loop>(&n)) {
            if (l->body.present())
                syncCounts(*l->body, nq, nc, physical);
        }
        if (auto* bx = std::get_if<Box>(&n)) {
            if (bx->body.present())
                syncCounts(*bx->body, nq, nc, physical);
        }
    }
}
} // namespace

Builder::Builder(const lang::Program& p, const ParamMap& inputs, const BuildOptions& opts)
    : p_(p), inputs_(inputs), opts_(opts) {}

Builder::Found Builder::find(std::string_view name) {
    for (std::size_t i = scopes_.size(); i-- > 0;)
        if (auto f = scopes_[i].find(name); f != scopes_[i].end())
            return Found{&f->second, i};
    return {};
}

std::string Builder::uniqueRegisterName(std::string_view base) const {
    auto taken = [&](const std::string& n) {
        if (p_.globals.contains(n) || p_.pulseOnlyGates.contains(n))
            return true;
        for (const auto* d : p_.defcals)
            if (d->gate == n)
                return true;
        for (const auto& r : circuit_.bitRegisters())
            if (r.name == n)
                return true;
        for (const auto& r : circuit_.qubitRegisters())
            if (r.name == n)
                return true;
        return false;
    };
    if (std::string b(base); !taken(b))
        return b;
    for (std::uint32_t k = 1;; ++k)
        if (std::string cand = std::format("{}__{}", base, k); !taken(cand))
            return cand;
}

CregRef Builder::allocRegister(std::string_view name, std::uint32_t width, RegKind kind,
                               bool scalar, bool global) {
    // The flat bit space follows declaration (lowering) order (spec 14 §3); bit `first + i` is the
    // little-endian bit i of the register.
    CregRef r{nextBit_, std::max<std::uint32_t>(width, 1)};
    nextBit_ += r.size;
    circuit_.setClbitCount(nextBit_);
    circuit_.addBitRegister(BitRegister{global ? std::string(name) : uniqueRegisterName(name),
                                        r.first, r.size, kind, scalar});
    return r;
}

Status Builder::setupRegisters() {
    const std::uint32_t physical = p_.usesPhysicalQubits ? maxPhysical(p_.ast.statements) : 0;
    circuit_.setQubitCount(std::max(p_.qubitCount, physical));
    circuit_.setPhysical(p_.usesPhysicalQubits);
    for (const auto& r : p_.qubitRegs) {
        const lang::Symbol* s = p_.symbol(r.name);
        circuit_.addQubitRegister(
            QubitRegister{r.name, r.first, r.size, !(s && s->type.isRegister)});
        Binding b;
        b.kind = Binding::Kind::Qubits;
        for (std::uint32_t i = 0; i < r.size; ++i)
            b.qubits.push_back(Wire{r.first + i});
        bind(r.name, std::move(b));
    }
    return {};
}

NodeId Builder::emit(Node n) {
    std::visit([&](auto& v) { v.span = stmtSpan_; }, n);
    return out().add(std::move(n));
}

Status Builder::countUnrolled(const SourceSpan& sp) {
    if (loopDepth_ == 0)
        return {};
    if (++unrolled_ > opts_.loopUnrollBound)
        return fail(diagnostic("QL4020", sp, unrolled_, opts_.loopUnrollBound));
    return {};
}

std::optional<std::string> Builder::literalOf(std::string_view globalName) const {
    if (scopes_.empty())
        return std::nullopt;
    auto it = scopes_.front().find(globalName);
    if (it == scopes_.front().end() || it->second.kind != Binding::Kind::Value)
        return std::nullopt;
    const Value& v = it->second.value;
    switch (v.kind) {
    case Value::Kind::Int:
        return std::format("{}", v.i);
    case Value::Kind::Float:
        return detail::formatFloatLiteral(v.f);
    case Value::Kind::Dur:
        return detail::qasmDuration(v.dur);
    case Value::Kind::Symbolic:
        return std::nullopt;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- driver
Result<Circuit> Builder::run() {
    if (!p_.ok()) {
        Error e(err::BadProgram, "program has errors; fix them before building the IR");
        for (const auto& x : p_.errors())
            e.withNote(x.format());
        return fail(std::move(e));
    }
    runtime_ = runtimeNames(p_);
    stack_.push_back(&circuit_);
    push();
    QXL_TRY(setupRegisters());
    QXL_TRY(lowerBody(p_.ast.statements));
    // Calibrations travel with the circuit, inputs and constants bound (spec 13 §6, 14 §6).
    core::Json cals = core::Json::array();
    for (const auto& s : p_.ast.statements)
        if (s && (s->is<lang::CalStmt>() || s->is<lang::DefcalStmt>()))
            cals.push_back(
                detail::calibrationText(*s, [this](std::string_view n) { return literalOf(n); }));
    pop();
    stack_.pop_back();
    syncCounts(circuit_, circuit_.qubitCount(), nextBit_, circuit_.isPhysical());
    auto& meta = circuit_.meta();
    meta["source"] = p_.ast.filename;
    if (p_.pragmas.device)
        meta["device"] = *p_.pragmas.device;
    if (p_.pragmas.shots)
        meta["shots"] = *p_.pragmas.shots;
    if (p_.pragmas.seed)
        meta["seed"] = *p_.pragmas.seed;
    meta["optimize"] = p_.pragmas.optimize;
    if (p_.pragmas.pulseLevel)
        meta["pulse_level"] = *p_.pragmas.pulseLevel;
    if (!cals.empty())
        meta["calibrations"] = std::move(cals);
    if (!p_.outputs.empty())
        meta["outputs"] = p_.outputs;
    return std::move(circuit_);
}

Status Builder::lowerBody(const StmtList& body) {
    for (const auto& s : body) {
        if (!s)
            continue;
        QXL_TRY(lowerStmt(*s));
        if (flow_ != Flow::Normal)
            break;
    }
    return {};
}

Status Builder::lowerStmt(const Stmt& s) {
    const SourceSpan outer = stmtSpan_;
    if (spanPin_ == 0)
        stmtSpan_ = s.span;
    Status st = countUnrolled(s.span);
    if (st)
        st = lowerStmtKind(s);
    // Build errors point at the innermost statement that failed (spec 14 §11 diagnostic format).
    if (!st && !st.error().span)
        st.error().withSpan(s.span);
    stmtSpan_ = outer;
    return st;
}

Status Builder::lowerStmtKind(const Stmt& s) {
    using namespace lang;
    if (s.is<VersionStmt>() || s.is<IncludeStmt>() || s.is<PragmaStmt>() || s.is<GateDecl>() ||
        s.is<DefStmt>() || s.is<QubitDecl>() || s.is<CalStmt>() || s.is<DefcalStmt>())
        return {}; // declarations: registered by Sema, expanded at use, or pulse level (spec 14 §6)
    if (s.is<EndStmt>()) {
        flow_ = Flow::End;
        return {};
    }
    if (s.is<BreakStmt>()) {
        flow_ = Flow::Break;
        return {};
    }
    if (s.is<ContinueStmt>()) {
        flow_ = Flow::Continue;
        return {};
    }
    if (const auto* n = s.as<ClassicalDecl>())
        return lowerDecl(*n);
    if (const auto* n = s.as<GateCall>())
        return lowerGateCall(*n, Applied{});
    if (const auto* n = s.as<MeasureStmt>())
        return lowerMeasureInto(n->qubits, n->target ? n->target->get() : nullptr);
    if (const auto* n = s.as<ResetStmt>()) {
        for (const auto& q : n->qubits) {
            QXL_TRY_ASSIGN(auto ws, qubitOperand(*q));
            for (Wire w : ws)
                emit(Reset{w, std::nullopt, {}});
        }
        return {};
    }
    if (const auto* n = s.as<BarrierStmt>()) {
        Barrier b;
        for (const auto& q : n->qubits) {
            QXL_TRY_ASSIGN(auto ws, qubitOperand(*q));
            b.wires.insert(b.wires.end(), ws.begin(), ws.end());
        }
        emit(std::move(b));
        return {};
    }
    if (const auto* n = s.as<DelayStmt>()) {
        Delay d;
        QXL_TRY_ASSIGN(d.duration, evalDuration(*n->duration));
        for (const auto& q : n->qubits) {
            QXL_TRY_ASSIGN(auto ws, qubitOperand(*q));
            d.wires.insert(d.wires.end(), ws.begin(), ws.end());
        }
        emit(std::move(d));
        return {};
    }
    if (const auto* n = s.as<BoxStmt>())
        return lowerBox(*n);
    if (const auto* n = s.as<AssignStmt>())
        return lowerAssign(*n);
    if (const auto* n = s.as<IfStmt>())
        return lowerIf(*n);
    if (const auto* n = s.as<ForStmt>())
        return lowerFor(*n);
    if (const auto* n = s.as<WhileStmt>())
        return lowerWhile(*n);
    if (const auto* n = s.as<ReturnStmt>()) {
        if (n->value) {
            QXL_TRY_ASSIGN(Value v, eval(**n->value));
            returned_ = std::move(v);
        }
        flow_ = Flow::Return;
        return {};
    }
    if (const auto* n = s.as<ExprStmt>()) {
        // Only calls and measurements have effects; any other expression statement is dropped.
        if (n->expr && (n->expr->is<CallExpr>() || n->expr->is<MeasureExpr>())) {
            if (const auto* m = n->expr->as<MeasureExpr>())
                return lowerMeasureInto(m->qubits, nullptr);
            QXL_TRY(eval(*n->expr));
        }
        return {};
    }
    return fail(
        Error(err::Unsupported, std::format("statement kind {} cannot be lowered", s.node.index()))
            .withSpan(s.span));
}

} // namespace qlab::ir::build

namespace qlab::ir {
Result<Circuit> buildCircuit(const lang::Program& p, const ParamMap& inputs,
                             const BuildOptions& opts) {
    build::Builder b(p, inputs, opts);
    return b.run();
}
} // namespace qlab::ir
