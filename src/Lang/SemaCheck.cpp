#include "Lang/Sema.hpp"
#include "Lang/SemaUtil.hpp"

// Spec 13 §3, §8 — expression checks: names, indices, calls, measurement dependence.
namespace qlab::lang {

// The pure built-in functions of spec 13 §3. Shared by call checking and constant evaluation.
bool isBuiltinMathFn(std::string_view name) {
    static constexpr std::string_view kFns[] = {"sin", "cos", "tan", "arcsin", "arccos", "arctan",
                                                "exp", "ln", "log", "sqrt", "floor", "ceil", "pow",
                                                "mod", "popcount", "rotl", "rotr", "sizeof", "real",
                                                "imag"};
    for (auto f : kFns) if (name == f) return true;
    return false;
}

bool Sema::dependsOnMeasurement(const Expr& e) const {
    if (e.is<MeasureExpr>()) return true;
    if (auto* n = e.as<Ident>()) return measuredBits_.contains(n->name);
    if (auto* n = e.as<IndexExpr>()) return dependsOnMeasurement(*n->base);
    if (auto* n = e.as<SliceExpr>()) return dependsOnMeasurement(*n->base);
    if (auto* n = e.as<UnaryExpr>()) return dependsOnMeasurement(*n->operand);
    if (auto* n = e.as<BinaryExpr>()) return dependsOnMeasurement(*n->lhs) || dependsOnMeasurement(*n->rhs);
    if (auto* n = e.as<CastExpr>()) return dependsOnMeasurement(*n->operand);
    if (auto* n = e.as<CallExpr>()) { if (measuringDefs_.contains(n->callee)) return true; for (auto& a : n->args) if (dependsOnMeasurement(*a)) return true; return false; }
    if (auto* n = e.as<ConcatExpr>()) return dependsOnMeasurement(*n->lhs) || dependsOnMeasurement(*n->rhs);
    return false;
}

void Sema::checkExpr(const Expr& e) {
    if (auto* n = e.as<Ident>()) {
        if (!lookup(n->name)) diag("QL3010", e.span, n->name);
        return;
    }
    if (auto* n = e.as<PhysicalQubitRef>()) { if (!p_.pragmas.physicalLayout() && !currentCallable_.starts_with("defcal ")) diag("QL3021", e.span, "$" + std::to_string(n->index)); p_.usesPhysicalQubits = true; return; }
    if (auto* n = e.as<IndexExpr>()) {
        checkExpr(*n->base); checkExpr(*n->index);
        if (auto* id = n->base->as<Ident>()) {
            const Symbol* s = lookup(id->name);
            if (s && !s->type.isRegister && s->kind != SymbolKind::GateQubit && s->kind != SymbolKind::DefParam && s->kind != SymbolKind::Frame && s->type.base != BaseType::Void) diag("QL3176", e.span, id->name);
            // size 0: the register size did not fold (already diagnosed), so the range is unknown
            if (s && s->type.isRegister && s->type.size > 0) { if (auto v = fold(*n->index)) { if (v->i < 0 || static_cast<std::size_t>(v->i) >= s->type.size) diag("QL3022", e.span, v->i, id->name, s->type.size); } }
        }
        return;
    }
    if (auto* n = e.as<SliceExpr>()) { checkExpr(*n->base); if (auto* r = n->range->as<RangeExpr>()) { checkExpr(*r->start); checkExpr(*r->stop); if (r->step) checkExpr(**r->step); } return; }
    if (auto* n = e.as<UnaryExpr>()) { checkExpr(*n->operand); return; }
    if (auto* n = e.as<BinaryExpr>()) {
        checkExpr(*n->lhs); checkExpr(*n->rhs);
        SemaType a = typeOf(*n->lhs), b = typeOf(*n->rhs);
        if ((a.base == BaseType::Duration) != (b.base == BaseType::Duration) && (n->op == BinaryOp::Add || n->op == BinaryOp::Sub) && a.base != BaseType::Void && b.base != BaseType::Void)
            diag("QL3160", e.span, typeName(a) + " and " + typeName(b));
        if ((a.base == BaseType::Qubit || b.base == BaseType::Qubit)) diag("QL3020", e.span, "arithmetic on a qubit");
        return;
    }
    if (auto* n = e.as<CastExpr>()) { checkExpr(*n->operand); if (n->type.base == BaseType::Complex) diag("QL3050", e.span); return; }
    if (auto* n = e.as<CallExpr>()) {
        for (auto& a : n->args) checkExpr(*a);
        if (isBuiltinMathFn(n->callee)) return;
        const Symbol* s = lookup(n->callee);
        if (!s) { diag("QL3010", e.span, n->callee); return; }
        if (s->kind != SymbolKind::Def) diag("QL3020", e.span, "'" + n->callee + "' is not callable");
        else if (!currentCallable_.empty()) gateCalls_[currentCallable_].push_back(n->callee);
        return;
    }
    if (auto* n = e.as<SetExpr>()) { for (auto& i : n->items) checkExpr(*i); return; }
    if (auto* n = e.as<RangeExpr>()) { checkExpr(*n->start); checkExpr(*n->stop); if (n->step) checkExpr(**n->step); return; }
    if (auto* n = e.as<ConcatExpr>()) { checkExpr(*n->lhs); checkExpr(*n->rhs); return; }
    if (auto* n = e.as<MeasureExpr>()) { std::vector<std::pair<std::string, std::int64_t>> seen; for (auto& q : n->qubits) checkQubitOperand(*q, seen); noteQuantum(e.span); sawMeasure_ = true; return; }
    // DurationOfExpr bodies are checked when the enclosing box is lowered; ErrorExpr is already reported.
}

// Spec 14 §3: `for` bounds and gate-modifier counts must be known when the compiler unrolls the
// program. Literals and `const` values qualify, and so do enclosing loop variables (an inner
// bound such as `[i-1:-1:0]` becomes constant once the outer loop is unrolled). Runtime classical
// variables, inputs and measurement results do not.
bool Sema::isCompileTimeKnown(const Expr& e) const {
    if (e.is<IntLit>() || e.is<FloatLit>() || e.is<BoolLit>() || e.is<ConstantRef>() ||
        e.is<DurationLit>() || e.is<ImagLit>() || e.is<BitStringLit>() || e.is<ErrorExpr>())
        return true;
    if (auto* n = e.as<Ident>()) {
        const Symbol* s = lookup(n->name);
        return s && (s->kind == SymbolKind::Const || s->kind == SymbolKind::LoopVar ||
                     s->kind == SymbolKind::GateParam);
    }
    if (auto* n = e.as<UnaryExpr>()) return isCompileTimeKnown(*n->operand);
    if (auto* n = e.as<BinaryExpr>()) return isCompileTimeKnown(*n->lhs) && isCompileTimeKnown(*n->rhs);
    if (auto* n = e.as<CastExpr>()) return isCompileTimeKnown(*n->operand);
    if (auto* n = e.as<CallExpr>()) {
        if (!isBuiltinMathFn(n->callee)) return false;
        for (auto& a : n->args) if (!isCompileTimeKnown(*a)) return false;
        return true;
    }
    return false;
}

} // namespace qlab::lang
