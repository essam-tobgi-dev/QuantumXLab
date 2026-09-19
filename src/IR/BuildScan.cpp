// Spec 14 §2 — pre-scan for per-shot classical names. A variable whose value can differ between
// shots must be storage in the flat bit space, decided before lowering so that its declaration
// allocates bits; everything else folds at build time.
#include "IR/BuildImpl.hpp"

namespace qlab::ir::build {
namespace {
constexpr const char* kMeasured = "*";

std::string targetName(const Expr& e) {
    if (const auto* n = e.as<lang::Ident>())
        return n->name;
    if (const auto* n = e.as<lang::IndexExpr>())
        return targetName(*n->base);
    if (const auto* n = e.as<lang::SliceExpr>())
        return targetName(*n->base);
    return {};
}

struct Scan {
    const lang::Program& p;
    std::set<std::string> bits;                           // declared `bit` variables
    std::set<std::string> measuringDefs;                  // defs that measure, transitively
    std::map<std::string, std::set<std::string>> returns; // def -> names its return values read
    std::map<std::string, std::set<std::string>> deps;    // name -> names its value depends on
    std::map<std::string, std::set<std::string>>
        callConditions; // def -> run-time condition names at call sites

    // Names an expression reads; kMeasured for a measurement or a call to a measuring def.
    void names(const Expr& e, std::set<std::string>& out) const {
        auto sub = [&](const lang::ExprPtr& x) {
            if (x)
                names(*x, out);
        };
        if (const auto* n = e.as<lang::Ident>())
            out.insert(n->name);
        if (e.is<lang::MeasureExpr>())
            out.insert(kMeasured);
        if (const auto* n = e.as<lang::IndexExpr>()) {
            sub(n->base);
            sub(n->index);
        }
        if (const auto* n = e.as<lang::SliceExpr>())
            sub(n->base);
        if (const auto* n = e.as<lang::UnaryExpr>())
            sub(n->operand);
        if (const auto* n = e.as<lang::BinaryExpr>()) {
            sub(n->lhs);
            sub(n->rhs);
        }
        if (const auto* n = e.as<lang::CastExpr>())
            sub(n->operand);
        if (const auto* n = e.as<lang::ConcatExpr>()) {
            sub(n->lhs);
            sub(n->rhs);
        }
        if (const auto* n = e.as<lang::CallExpr>()) {
            for (const auto& a : n->args)
                sub(a);
            if (measuringDefs.contains(n->callee))
                out.insert(kMeasured);
            if (auto r = returns.find(n->callee); r != returns.end())
                out.insert(r->second.begin(), r->second.end());
        }
    }
    // Every def called inside `e` inherits the run-time conditions of the call site.
    void calls(const Expr& e, const std::set<std::string>& conds) {
        auto sub = [&](const lang::ExprPtr& x) {
            if (x)
                calls(*x, conds);
        };
        if (const auto* n = e.as<lang::CallExpr>()) {
            if (p.defs.contains(n->callee))
                callConditions[n->callee].insert(conds.begin(), conds.end());
            for (const auto& a : n->args)
                sub(a);
        }
        if (const auto* n = e.as<lang::UnaryExpr>())
            sub(n->operand);
        if (const auto* n = e.as<lang::BinaryExpr>()) {
            sub(n->lhs);
            sub(n->rhs);
        }
        if (const auto* n = e.as<lang::CastExpr>())
            sub(n->operand);
        if (const auto* n = e.as<lang::IndexExpr>()) {
            sub(n->base);
            sub(n->index);
        }
    }
    bool measures(const StmtList& body) const {
        for (const auto& sp : body) {
            if (!sp)
                continue;
            const Stmt& s = *sp;
            std::set<std::string> read;
            if (s.is<lang::MeasureStmt>())
                return true;
            if (const auto* n = s.as<lang::ClassicalDecl>(); n && n->init)
                names(**n->init, read);
            if (const auto* n = s.as<lang::AssignStmt>())
                names(*n->value, read);
            if (const auto* n = s.as<lang::ExprStmt>())
                names(*n->expr, read);
            if (const auto* n = s.as<lang::ReturnStmt>(); n && n->value)
                names(**n->value, read);
            if (const auto* n = s.as<lang::GateCall>(); n && measuringDefs.contains(n->name))
                return true;
            if (const auto* n = s.as<lang::IfStmt>()) {
                names(*n->cond, read);
                if (measures(n->thenBody) || measures(n->elseBody))
                    return true;
            }
            if (const auto* n = s.as<lang::ForStmt>(); n && measures(n->body))
                return true;
            if (const auto* n = s.as<lang::WhileStmt>()) {
                names(*n->cond, read);
                if (measures(n->body))
                    return true;
            }
            if (const auto* n = s.as<lang::BoxStmt>(); n && measures(n->body))
                return true;
            if (read.contains(kMeasured))
                return true;
        }
        return false;
    }
    void returnNames(const StmtList& body, std::set<std::string>& out) const {
        for (const auto& sp : body) {
            if (!sp)
                continue;
            if (const auto* n = sp->as<lang::ReturnStmt>(); n && n->value)
                names(**n->value, out);
            if (const auto* n = sp->as<lang::IfStmt>()) {
                returnNames(n->thenBody, out);
                returnNames(n->elseBody, out);
            }
            if (const auto* n = sp->as<lang::ForStmt>())
                returnNames(n->body, out);
            if (const auto* n = sp->as<lang::WhileStmt>())
                returnNames(n->body, out);
            if (const auto* n = sp->as<lang::BoxStmt>())
                returnNames(n->body, out);
        }
    }
    // dst depends on the names of its value and on every enclosing run-time condition.
    void edges(const StmtList& body, const std::set<std::string>& conds) {
        for (const auto& sp : body) {
            if (!sp)
                continue;
            const Stmt& s = *sp;
            auto assign = [&](const std::string& dst, const Expr* src) {
                if (dst.empty())
                    return;
                auto& d = deps[dst];
                d.insert(conds.begin(), conds.end());
                if (src) {
                    names(*src, d);
                    calls(*src, conds);
                }
            };
            if (const auto* n = s.as<lang::ClassicalDecl>()) {
                if (n->type.base == lang::BaseType::Bit)
                    bits.insert(n->name);
                assign(n->name, n->init ? n->init->get() : nullptr);
            }
            if (const auto* n = s.as<lang::AssignStmt>())
                assign(targetName(*n->target), n->value.get());
            if (const auto* n = s.as<lang::MeasureStmt>(); n && n->target)
                deps[targetName(**n->target)].insert(kMeasured);
            if (const auto* n = s.as<lang::ExprStmt>())
                calls(*n->expr, conds);
            if (const auto* n = s.as<lang::ReturnStmt>(); n && n->value)
                calls(**n->value, conds);
            if (const auto* n = s.as<lang::GateCall>(); n && p.defs.contains(n->name))
                callConditions[n->name].insert(conds.begin(), conds.end());
            if (const auto* n = s.as<lang::IfStmt>()) {
                std::set<std::string> inner = conds;
                names(*n->cond, inner);
                calls(*n->cond, conds);
                edges(n->thenBody, inner);
                edges(n->elseBody, inner);
            }
            if (const auto* n = s.as<lang::WhileStmt>()) {
                std::set<std::string> inner = conds;
                names(*n->cond, inner);
                calls(*n->cond, conds);
                edges(n->body, inner);
            }
            if (const auto* n = s.as<lang::ForStmt>())
                edges(n->body, conds);
            if (const auto* n = s.as<lang::BoxStmt>())
                edges(n->body, conds);
        }
    }
};
} // namespace

std::set<std::string> runtimeNames(const lang::Program& p) {
    Scan scan{p, {}, {}, {}, {}, {}};
    // Defs that measure and the names their return values read, to a fixpoint over nested calls.
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& [name, def] : p.defs) {
            if (!scan.measuringDefs.contains(name) && scan.measures(def->body)) {
                scan.measuringDefs.insert(name);
                changed = true;
            }
            std::set<std::string> ret;
            scan.returnNames(def->body, ret);
            if (auto& cur = scan.returns[name]; cur.size() != ret.size()) {
                cur = std::move(ret);
                changed = true;
            }
        }
    }
    scan.edges(p.ast.statements, {});
    // Def bodies see the conditions of their call sites; nested calls need a few passes.
    for (std::size_t pass = 0; pass <= p.defs.size(); ++pass)
        for (const auto& [name, def] : p.defs)
            scan.edges(def->body, scan.callConditions[name]);
    std::set<std::string> runtime = scan.bits;
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& [dst, srcs] : scan.deps) {
            if (runtime.contains(dst))
                continue;
            for (const auto& s : srcs)
                if (s == kMeasured || runtime.contains(s)) {
                    runtime.insert(dst);
                    changed = true;
                    break;
                }
        }
    }
    return runtime;
}

} // namespace qlab::ir::build
