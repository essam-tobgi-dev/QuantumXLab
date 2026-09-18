#include "Lang/Sema.hpp"
#include "Lang/SemaUtil.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

// Spec 13 §3, §5 — quantum and classical statements, control flow.
namespace qlab::lang {

void Sema::noteQuantum(const SourceSpan&) {
    sawQuantum_ = true; p_.hasQuantumStatements = true;
    if (sawMeasure_) p_.hasMidCircuitMeasurement = true;
}

std::size_t Sema::checkQubitOperand(const Expr& e, std::vector<std::pair<std::string, std::int64_t>>& seen) {
    // Index sentinels: >= 0 is a constant index, kWholeReg the whole register, kUnknownIdx an
    // index that is not compile-time constant. Two operands alias only when one covers the whole
    // register, or both are constant and equal; `q[i], q[i+1]` must not be reported (spec 13 §4).
    // A register of size 0 has an unknown size (its declaration was already diagnosed).
    constexpr std::int64_t kWholeReg = -1, kUnknownIdx = -2;
    auto dup = [&](const std::string& name, std::int64_t idx) {
        for (auto& [n, i] : seen) {
            if (n != name) continue;
            bool aliases = i == kWholeReg || idx == kWholeReg || (i >= 0 && idx >= 0 && i == idx);
            if (aliases) {
                diag("QL3140", e.span, idx >= 0 ? name + "[" + std::to_string(idx) + "]" : name);
                return;
            }
        }
        seen.emplace_back(name, idx);
    };
    if (e.is<ErrorExpr>()) return 0; // already reported by the parser
    if (auto* pq = e.as<PhysicalQubitRef>()) { checkExpr(e); dup("$" + std::to_string(pq->index), kWholeReg); return 1; }
    if (auto* id = e.as<Ident>()) {
        const Symbol* s = lookup(id->name);
        if (!s) { diag("QL3010", e.span, id->name); return 0; }
        if (s->type.base != BaseType::Qubit) { diag("QL3171", e.span, id->name); return 0; }
        if (s->kind == SymbolKind::QubitReg || s->kind == SymbolKind::DefParam) { if (p_.usesPhysicalQubits && p_.pragmas.physicalLayout()) diag("QL3021", e.span, id->name); }
        dup(id->name, s->type.isRegister ? kWholeReg : 0);
        return s->type.isRegister ? s->type.size : 1;
    }
    if (auto* ix = e.as<IndexExpr>()) {
        auto* id = ix->base->as<Ident>();
        if (!id) { diag("QL3171", e.span, detail::exprText(e)); return 0; }
        const Symbol* s = lookup(id->name);
        if (!s) { diag("QL3010", e.span, id->name); return 0; }
        if (s->type.base != BaseType::Qubit) { diag("QL3171", e.span, id->name); return 0; }
        checkExpr(*ix->index);
        auto v = fold(*ix->index);
        if (v && s->type.isRegister && s->type.size > 0 && (v->i < 0 || static_cast<std::size_t>(v->i) >= s->type.size)) { diag("QL3022", e.span, v->i, id->name, s->type.size); return 0; }
        dup(id->name, v ? v->i : kUnknownIdx);
        return 1;
    }
    if (auto* sl = e.as<SliceExpr>()) {
        auto* id = sl->base->as<Ident>(); const Symbol* s = id ? lookup(id->name) : nullptr;
        if (!s || s->type.base != BaseType::Qubit) { diag("QL3171", e.span, id ? id->name : detail::exprText(e)); return 0; }
        auto* r = sl->range->as<RangeExpr>();
        auto a = r ? fold(*r->start) : std::nullopt;
        auto b = r ? fold(*r->stop) : std::nullopt;
        auto step = r && r->step ? fold(**r->step) : std::optional<ConstValue>{};
        if (!a || !b || (r->step && (!step || step->i == 0))) { dup(id->name, kWholeReg); return s->type.size; }
        // Inclusive range start:step:stop (spec 13 §3); a reversed or out-of-range selection is QL3022.
        const std::int64_t st = step ? step->i : 1;
        auto beyond = [&](std::int64_t i) { return i < 0 || (s->type.size > 0 && static_cast<std::size_t>(i) >= s->type.size); };
        if (beyond(a->i) || beyond(b->i) || (st > 0 ? b->i < a->i : b->i > a->i)) {
            diag("QL3022", e.span, beyond(a->i) ? a->i : b->i, id->name, s->type.size);
            return 0;
        }
        dup(id->name, kWholeReg);
        return static_cast<std::size_t>((b->i - a->i) / st) + 1;
    }
    diag("QL3171", e.span, detail::exprText(e));
    return 0;
}

void Sema::visitBody(const StmtList& b) { for (auto& s : b) if (s) visitStmt(*s); }

void Sema::visitGateCall(const GateCall& c, const SourceSpan& sp) {
    const Symbol* us = lookup(c.name);
    const bool userGate = us && us->kind == SymbolKind::Gate;
    const GateInfo* gi = StdGates::find(c.name);
    // A user gate overrides a native device gate of the same name. A rejected redefinition of a
    // stdgates name (QL3061) stands in for the library gate when stdgates.inc is not included.
    if (gi && userGate && (gi->kind == GateKind::Native || (gi->kind != GateKind::Builtin && !p_.includesStdGates))) gi = nullptr;
    int nParams = 0, nQubits = 0; bool rotation = false;
    if (gi) { nParams = gi->nParams; nQubits = gi->nQubits; rotation = gi->isRotation; if (gi->kind == GateKind::Standard || gi->kind == GateKind::Legacy) { if (!p_.includesStdGates) diag("QL3010", sp, c.name); } }
    else if (userGate) { nParams = us->nParams; nQubits = us->nQubits; if (!currentCallable_.empty()) gateCalls_[currentCallable_].push_back(c.name); }
    else if (us) { diag("QL3170", sp, c.name); return; }
    else if (auto dc = defcalGates_.find(c.name); dc != defcalGates_.end()) {
        // Pulse-level-only gate: its signature comes from the calibration (spec 13 §5).
        nParams = static_cast<int>(dc->second->paramNames.empty() ? dc->second->params.size()
                                                                 : dc->second->paramNames.size());
        nQubits = static_cast<int>(dc->second->qubits.size());
        p_.pulseOnlyGates.insert(c.name);
    }
    else { diag("QL3010", sp, c.name); return; }
    p_.usedGates.insert(c.name);
    int extraControls = 0;
    for (auto& m : c.modifiers) {
        if (m.arg) { checkExpr(**m.arg); auto v = fold(**m.arg);
            if (m.kind == ModifierKind::Pow) { if (!v) diag("QL3040", sp, "pow exponent"); else if (!rotation && v->kind == ConstValue::Float && std::abs(v->f - std::round(v->f)) > 1e-12) diag("QL3070", sp, c.name); }
            else { if (!v || !v->isNumeric() || v->i < 1) diag("QL3040", sp, "ctrl count"); else extraControls += static_cast<int>(std::min<std::int64_t>(v->i, 4096)); } }
        else if (m.kind == ModifierKind::Ctrl || m.kind == ModifierKind::NegCtrl) extraControls += 1;
    }
    for (auto& p : c.params) { checkExpr(*p); SemaType t = typeOf(*p); if (t.base == BaseType::Qubit || t.base == BaseType::Bit) diag("QL3020", p->span, "gate parameter of type " + typeName(t)); }
    if (static_cast<int>(c.params.size()) != nParams) diag("QL3151", sp, c.name, nParams, c.params.size());
    std::vector<std::pair<std::string, std::int64_t>> seen;
    std::size_t regSize = 0; bool sizeMismatch = false;
    for (auto& q : c.qubits) { std::size_t n = checkQubitOperand(*q, seen); if (n > 1) { if (regSize && regSize != n) sizeMismatch = true; regSize = n; } }
    if (sizeMismatch) diag("QL3175", sp);
    int want = nQubits + extraControls;
    if (want > 0 && static_cast<int>(c.qubits.size()) != want) diag("QL3150", sp, c.name, want, c.qubits.size());
    if (!currentCallable_.empty() && p_.userGates.contains(currentCallable_)) return; // inside gate body: not a program-level quantum statement
    noteQuantum(sp);
    if (sawMeasure_) p_.hasMidCircuitMeasurement = true;
}

void Sema::visitMeasure(const MeasureStmt& m, const SourceSpan& sp) {
    std::vector<std::pair<std::string, std::int64_t>> seen;
    std::size_t nq = 0; for (auto& q : m.qubits) nq += checkQubitOperand(*q, seen);
    if (m.target) {
        checkExpr(**m.target);
        SemaType t = typeOf(**m.target);
        if (t.base != BaseType::Bit && t.base != BaseType::Void) diag("QL3020", sp, "measurement target must be a bit, got " + typeName(t));
        std::size_t nb = t.isRegister ? t.size : 1;
        if (t.base == BaseType::Bit && nq && nb && nb != nq) diag("QL3175", sp);
        if (auto* id = (*m.target)->as<Ident>()) measuredBits_.insert(id->name);
        if (auto* ix = (*m.target)->as<IndexExpr>()) if (auto* id = ix->base->as<Ident>()) measuredBits_.insert(id->name);
    } else { diag("QL3030", sp, m.qubits.empty() ? std::string() : detail::exprText(*m.qubits.front())); }
    noteQuantum(sp); sawMeasure_ = true;
}

void Sema::visitAssign(const AssignStmt& a, const SourceSpan& sp) {
    checkExpr(*a.target); checkExpr(*a.value);
    std::string name;
    if (auto* id = a.target->as<Ident>()) name = id->name;
    else if (auto* ix = a.target->as<IndexExpr>()) { if (auto* bid = ix->base->as<Ident>()) name = bid->name; }
    else if (auto* sl = a.target->as<SliceExpr>()) { if (auto* bid = sl->base->as<Ident>()) name = bid->name; }
    const Symbol* s = name.empty() ? nullptr : lookup(name);
    if (!s) return;
    if (s->kind == SymbolKind::Const) diag("QL3172", sp, name, "it is const");
    else if (s->kind == SymbolKind::Input) diag("QL3172", sp, name, "it is an input");
    else if (s->kind == SymbolKind::LoopVar) diag("QL3172", sp, name, "it is a loop variable");
    else if (s->kind == SymbolKind::QubitReg || s->kind == SymbolKind::GateQubit) diag("QL3172", sp, name, "it is a qubit");
    SemaType vt = typeOf(*a.value);
    if (a.value->is<MeasureExpr>()) { if (s->type.base != BaseType::Bit) diag("QL3020", sp, "measurement result assigned to " + typeName(s->type)); }
    else if (vt.base == BaseType::Qubit) diag("QL3020", sp, "cannot assign a qubit");
    else if (s->type.base == BaseType::Duration && vt.base != BaseType::Duration && vt.base != BaseType::Void) diag("QL3020", sp, "duration assigned from " + typeName(vt));
    else if ((s->type.base == BaseType::Int || s->type.base == BaseType::Uint) && vt.base == BaseType::Duration) diag("QL3020", sp, "duration assigned to " + typeName(s->type));
    if (dependsOnMeasurement(*a.value)) measuredBits_.insert(name);
}

void Sema::visitIf(const IfStmt& s, const SourceSpan& sp) {
    checkExpr(*s.cond);
    SemaType ct = typeOf(*s.cond);
    if (ct.base == BaseType::Bit && ct.isRegister && !s.cond->is<BinaryExpr>()) { std::string n = s.cond->as<Ident>() ? s.cond->as<Ident>()->name : "register"; diag("QL3080", sp, n); }
    if (dependsOnMeasurement(*s.cond)) p_.hasFeedforward = true;
    pushScope(); visitBody(s.thenBody); popScope();
    if (s.hasElse) { pushScope(); visitBody(s.elseBody); popScope(); }
}

void Sema::visitFor(const ForStmt& f, const SourceSpan& sp) {
    if (auto* r = f.iterable->as<RangeExpr>()) { checkExpr(*r->start); checkExpr(*r->stop); if (r->step) checkExpr(**r->step);
        // Bounds need not fold here: an inner bound may reference an enclosing loop variable,
        // which becomes constant when the compiler unrolls the outer loop (spec 14 §3).
        if (!isCompileTimeKnown(*r->start) || !isCompileTimeKnown(*r->stop) ||
            (r->step && !isCompileTimeKnown(**r->step)))
            diag("QL3040", sp, "for range bounds"); }
    else checkExpr(*f.iterable);
    pushScope();
    Symbol lv; lv.name = f.var; lv.kind = SymbolKind::LoopVar; lv.type.base = f.type.base; lv.span = sp; declare(lv);
    ++loopDepth_; visitBody(f.body); --loopDepth_;
    popScope();
}

void Sema::visitWhile(const WhileStmt& w, const SourceSpan& sp) {
    checkExpr(*w.cond);
    bool runtime = dependsOnMeasurement(*w.cond);
    if (runtime) p_.hasFeedforward = true;
    if (!runtime && !fold(*w.cond) && !detail::containsErrorExpr(*w.cond)) {
        // Statically bounded if the condition reads a variable assigned in the body from constants: accept int/uint loop counters.
        bool counter = false;
        std::function<void(const StmtList&)> scan = [&](const StmtList& b) { for (auto& st : b) { if (!st) continue; if (auto* a = st->as<AssignStmt>()) { if (auto* id = a->target->as<Ident>()) { const Symbol* s = lookup(id->name); if (s && (s->type.base == BaseType::Int || s->type.base == BaseType::Uint)) counter = true; } } if (auto* i = st->as<IfStmt>()) { scan(i->thenBody); scan(i->elseBody); } } };
        scan(w.body);
        if (!counter) diag("QL3090", sp);
    }
    pushScope(); ++loopDepth_; visitBody(w.body); --loopDepth_; popScope();
}

} // namespace qlab::lang
