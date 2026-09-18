// Spec 14 §2 — control-flow lowering: nested bodies, if → Branch, loop unrolling with its bound
// (QL4020), run-time while → Loop, and box.
#include "IR/BuildImpl.hpp"
#include <algorithm>
#include <cstdint>

namespace qlab::ir::build {

Result<Circuit> Builder::subCircuit(const StmtList& body, bool conditional) {
    Circuit sub;
    sub.setQubitCount(circuit_.qubitCount());
    sub.setClbitCount(nextBit_);
    sub.setPhysical(circuit_.isPhysical());
    stack_.push_back(&sub);
    push();
    const std::size_t savedCondition = conditionScope_;
    if (conditional) conditionScope_ = scopes_.size() - 1;
    Status st = lowerBody(body);
    conditionScope_ = savedCondition;
    pop();
    stack_.pop_back();
    QXL_TRY(st);
    if (conditional && flow_ != Flow::Normal)
        return fail(err::Unsupported, "break, continue, return and end are not supported inside a run-time branch or loop");
    return sub;
}

Status Builder::lowerIf(const lang::IfStmt& s) {
    QXL_TRY_ASSIGN(Value c, eval(*s.cond));
    if (c.constant()) {
        // Folded at build time: inputs are bound and constants known (spec 14 §2).
        const bool taken = c.kind == Value::Kind::Dur ? !c.dur.zero() : c.asDouble() != 0.0;
        push();
        Status st = lowerBody(taken ? s.thenBody : s.elseBody);
        pop();
        return st;
    }
    Branch br;
    br.cond = std::move(c.sym);
    QXL_TRY_ASSIGN(Circuit thenC, subCircuit(s.thenBody, true));
    br.thenBody = SubCircuit(std::move(thenC));
    if (s.hasElse) {
        QXL_TRY_ASSIGN(Circuit elseC, subCircuit(s.elseBody, true));
        br.elseBody = SubCircuit(std::move(elseC));
    }
    emit(std::move(br));
    return {};
}

Status Builder::lowerFor(const lang::ForStmt& f) {
    std::vector<Value> set;
    Range rg;
    if (const auto* r = f.iterable->as<lang::RangeExpr>()) {
        QXL_TRY_ASSIGN(rg, range(*r));
    } else if (const auto* s = f.iterable->as<lang::SetExpr>()) {
        for (const auto& item : s->items) {
            QXL_TRY_ASSIGN(Value v, eval(*item));
            if (!v.constant()) return fail(err::NotConstant, "for-loop set members must be known at build time");
            set.push_back(std::move(v));
        }
        rg.count = set.size();
    } else {
        return fail(err::Unsupported, "for-loop iterable must be a range or a set");
    }
    // QL4020 before unrolling anything: the loop alone expands to count × body statements.
    const std::uint64_t perIteration = std::max<std::uint64_t>(f.body.size(), 1);
    if (rg.count > opts_.loopUnrollBound / perIteration) {
        const std::uint64_t stmts = rg.count > UINT64_MAX / perIteration ? UINT64_MAX : rg.count * perIteration;
        return fail(diagnostic("QL4020", stmtSpan_, stmts, opts_.loopUnrollBound));
    }
    ++loopDepth_;
    Status st;
    for (std::uint64_t i = 0; i < rg.count; ++i) {
        push();
        Binding b;
        b.value = set.empty() ? Value::ofInt(rg.start + static_cast<std::int64_t>(i) * rg.step) : set[i];
        bind(f.var, std::move(b));
        st = lowerBody(f.body);
        pop();
        if (!st) break;
        if (flow_ == Flow::Break) { flow_ = Flow::Normal; break; }
        if (flow_ == Flow::Continue) flow_ = Flow::Normal;
        if (flow_ == Flow::Return || flow_ == Flow::End) break;
    }
    --loopDepth_;
    return st;
}

Status Builder::lowerWhile(const lang::WhileStmt& w) {
    const SourceSpan sp = stmtSpan_;
    QXL_TRY_ASSIGN(Value c, eval(*w.cond));
    if (!c.constant()) {
        // The condition depends on measurement: a control node executed per shot (spec 15 §3).
        Loop lp;
        lp.cond = std::move(c.sym);
        lp.maxIterations = opts_.loopUnrollBound;
        QXL_TRY_ASSIGN(Circuit body, subCircuit(w.body, true));
        lp.body = SubCircuit(std::move(body));
        emit(std::move(lp));
        return {};
    }
    ++loopDepth_;
    Status st;
    for (std::uint64_t iterations = 0;; ++iterations) {
        auto cur = eval(*w.cond);
        if (!cur) { st = std::unexpected(cur.error()); break; }
        if (!cur->constant()) { st = fail(err::NotConstant, "while condition became run-time dependent while unrolling"); break; }
        if (cur->asDouble() == 0.0) break;
        if (iterations >= opts_.loopUnrollBound) {
            const std::uint64_t stmts = (iterations + 1) * std::max<std::uint64_t>(w.body.size(), 1);
            st = fail(diagnostic("QL4020", sp, stmts, opts_.loopUnrollBound));
            break;
        }
        push();
        st = lowerBody(w.body);
        pop();
        if (!st) break;
        if (flow_ == Flow::Break) { flow_ = Flow::Normal; break; }
        if (flow_ == Flow::Continue) flow_ = Flow::Normal;
        if (flow_ == Flow::Return || flow_ == Flow::End) break;
    }
    --loopDepth_;
    return st;
}

Status Builder::lowerBox(const lang::BoxStmt& b) {
    Box box;
    if (b.duration) { QXL_TRY_ASSIGN(Duration d, evalDuration(**b.duration)); box.duration = d; }
    QXL_TRY_ASSIGN(Circuit body, subCircuit(b.body, false));
    box.body = SubCircuit(std::move(body));
    emit(std::move(box));
    return {};
}

} // namespace qlab::ir::build
