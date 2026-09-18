#include "Lang/Sema.hpp"
#include "Lang/SemaUtil.hpp"
#include <algorithm>

// Spec 13 §3, §5, §7 — scopes, statement dispatch, whole-program rules and the entry points.
namespace qlab::lang {

std::string typeName(const SemaType& t) {
    std::string s = baseTypeName(t.base);
    if (t.width) s += "[" + std::to_string(t.width) + "]";
    if (t.isRegister) s += "[" + std::to_string(t.size) + "]";
    return s;
}

const Symbol* Program::symbol(std::string_view name) const {
    auto it = globals.find(std::string(name));
    return it == globals.end() ? nullptr : &it->second;
}
std::optional<std::uint32_t> Program::qubitIndex(std::string_view reg, std::size_t element) const {
    for (auto& r : qubitRegs) if (r.name == reg) return element < r.size ? std::optional<std::uint32_t>(r.first + static_cast<std::uint32_t>(element)) : std::nullopt;
    return std::nullopt;
}

const Symbol* Sema::lookup(std::string_view name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->syms.find(std::string(name));
        if (f != it->syms.end()) return &f->second;
    }
    return nullptr;
}
Symbol* Sema::lookupMut(std::string_view name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->syms.find(std::string(name));
        if (f != it->syms.end()) return &f->second;
    }
    return nullptr;
}
bool Sema::declare(Symbol s) {
    Scope& cur = scopes_.back();
    if (cur.syms.contains(s.name)) { diag("QL3011", s.span, s.name); return false; }
    if (scopes_.size() > 1 && scopes_.front().syms.contains(s.name) && cur.isBlock && !cur.isGateBody && !cur.isDef) {
        diag("QL3012", s.span, s.name); return false;
    }
    s.isGlobal = scopes_.size() == 1;
    if (s.isGlobal) p_.globals[s.name] = s;
    cur.syms[s.name] = std::move(s);
    return true;
}

void Sema::visitStmt(const Stmt& s) {
    const SourceSpan& sp = s.span;
    if (auto* n = s.as<IncludeStmt>()) { if (n->path == "stdgates.inc") p_.includesStdGates = true; else diag("QL2002", sp, n->path); return; }
    if (s.is<VersionStmt>() || s.is<PragmaStmt>() || s.is<EndStmt>()) return;
    if (auto* n = s.as<QubitDecl>()) { if (scopes_.size() > 1) diag("QL3020", sp, "qubit declarations are global-scope only"); visitQubitDecl(*n, sp); return; }
    if (auto* n = s.as<ClassicalDecl>()) { visitDecl(*n, sp); return; }
    if (auto* n = s.as<GateDecl>()) { visitGateDecl(*n, sp); return; }
    if (auto* n = s.as<DefStmt>()) { visitDef(*n, sp); return; }
    if (s.is<ExternStmt>()) { diag("QL3100", sp); return; }
    if (auto* n = s.as<GateCall>()) { visitGateCall(*n, sp); return; }
    if (auto* n = s.as<MeasureStmt>()) { visitMeasure(*n, sp); return; }
    if (auto* n = s.as<ResetStmt>()) { std::vector<std::pair<std::string, std::int64_t>> seen; for (auto& q : n->qubits) checkQubitOperand(*q, seen); noteQuantum(sp); return; }
    if (auto* n = s.as<BarrierStmt>()) { std::vector<std::pair<std::string, std::int64_t>> seen; for (auto& q : n->qubits) checkQubitOperand(*q, seen); return; }
    if (auto* n = s.as<DelayStmt>()) { checkExpr(n->duration); SemaType t = typeOf(n->duration); if (t.base != BaseType::Duration && t.base != BaseType::Void) diag("QL3020", sp, "delay needs a duration, got " + typeName(t)); std::vector<std::pair<std::string, std::int64_t>> seen; for (auto& q : n->qubits) checkQubitOperand(*q, seen); noteQuantum(sp); return; }
    if (auto* n = s.as<BoxStmt>()) { if (n->duration) checkExpr(**n->duration); pushScope(); visitBody(n->body); popScope(); return; }
    if (auto* n = s.as<AssignStmt>()) { visitAssign(*n, sp); return; }
    if (auto* n = s.as<ExprStmt>()) {
        // `mygate;` / `mygate(0.1);` parses as an expression statement because the parser cannot know
        // user or calibration-only gate names: report the missing operands, not a type error.
        const Ident* id = n->expr->as<Ident>();
        const CallExpr* call = n->expr->as<CallExpr>();
        const std::string gateName = id ? id->name : call ? call->callee : std::string();
        const Symbol* g = gateName.empty() ? nullptr : lookup(gateName);
        int operands = -1;
        if (g && g->kind == SymbolKind::Gate) operands = g->nQubits;
        else if (!g && !gateName.empty()) if (auto dc = defcalGates_.find(gateName); dc != defcalGates_.end()) operands = static_cast<int>(dc->second->qubits.size());
        if (operands >= 0) {
            if (call) for (auto& a : call->args) checkExpr(*a);
            diag("QL3150", sp, gateName, operands, 0);
            return;
        }
        checkExpr(*n->expr);
        if (auto* m = n->expr->as<MeasureExpr>()) diag("QL3030", sp, m->qubits.empty() ? std::string() : detail::exprText(*m->qubits.front()));
        return;
    }
    if (auto* n = s.as<IfStmt>()) { visitIf(*n, sp); return; }
    if (auto* n = s.as<ForStmt>()) { visitFor(*n, sp); return; }
    if (auto* n = s.as<WhileStmt>()) { visitWhile(*n, sp); return; }
    if (s.is<SwitchStmt>()) { diag("QL3095", sp); return; }
    if (auto* n = s.as<ReturnStmt>()) { if (defDepth_ == 0) diag("QL3173", sp); if (n->value) checkExpr(**n->value); return; }
    if (s.is<BreakStmt>()) { if (loopDepth_ == 0) diag("QL3174", sp, "break"); return; }
    if (s.is<ContinueStmt>()) { if (loopDepth_ == 0) diag("QL3174", sp, "continue"); return; }
    if (auto* n = s.as<CalStmt>()) { std::set<std::string> frames; pushScope(true, false, true); visitCalBody(n->body, frames); popScope(); for (auto& f : frames) { Symbol fs; fs.name = f; fs.kind = SymbolKind::Frame; fs.span = sp; scopes_.front().syms[f] = fs; p_.globals[f] = fs; } return; }
    if (auto* n = s.as<DefcalStmt>()) { visitDefcal(*n, sp); return; }
}

void Sema::checkRecursion() {
    for (auto& [name, callees] : gateCalls_) {
        std::set<std::string> visited; std::vector<std::string> stack{name};
        bool recursive = false;
        while (!stack.empty() && !recursive) {
            std::string cur = stack.back(); stack.pop_back();
            for (auto& c : gateCalls_[cur]) {
                if (c == name) { recursive = true; break; }
                if (visited.insert(c).second) stack.push_back(c);
            }
        }
        if (!recursive) continue;
        const Symbol* s = lookup(name);
        diag("QL3060", s ? s->span : SourceSpan{}, s && s->kind == SymbolKind::Def ? "def" : "gate", name);
    }
}

void Sema::run() {
    scopes_.clear(); pushScope(false);
    // Pragmas are hoisted (spec 13 §7): interpret them first so `qlab.layout physical` and sweeps are known.
    for (auto& st : p_.ast.statements) if (st) if (auto* pr = st->as<PragmaStmt>()) applyPragma(*pr, st->span, p_.pragmas, p_.diagnostics);
    // A `defcal` may calibrate a gate that has no `gate` declaration (spec 13 §5–§6): such gates
    // exist only at pulse level. Collect them first so a call before the defcal still resolves.
    for (auto& st : p_.ast.statements)
        if (st) if (auto* dc = st->as<DefcalStmt>()) defcalGates_.emplace(dc->gate, dc);
    for (auto& st : p_.ast.statements) if (st) visitStmt(*st);
    checkRecursion();
    // Spec 13 §7: a `qlab.sweep` names an input declared before it. Only sweeps applyPragma accepted
    // are checked (a malformed one already carries QL2014).
    for (auto& st : p_.ast.statements) {
        const PragmaStmt* pr = st ? st->as<PragmaStmt>() : nullptr;
        if (!pr || !pr->isQlab || pr->name != "sweep" || pr->args.empty()) continue;
        const std::string& target = pr->args.front();
        if (std::none_of(p_.pragmas.sweeps.begin(), p_.pragmas.sweeps.end(), [&](const Sweep& sw) { return sw.input == target; })) continue;
        auto in = std::find_if(p_.inputs.begin(), p_.inputs.end(), [&](const InputVar& v) { return v.name == target; });
        if (in == p_.inputs.end()) diag("QL3010", st->span, target, "(qlab.sweep target is not an input)");
        else if (std::pair(in->span.line, in->span.column) > std::pair(st->span.line, st->span.column))
            diag("QL3010", st->span, target, "(declare the input before its qlab.sweep)");
    }
    // Spec 13 §5: QL3002 accompanies a program that compiles to an empty circuit. A program with
    // errors does not compile, and error recovery may have dropped its quantum statements.
    if (!p_.hasQuantumStatements && p_.outputs.empty() && !p_.pragmas.rb && !hasErrors(p_.diagnostics)) diag("QL3002", SourceSpan{});
    popScope();
}

Program analyzeProgram(std::string_view text, std::string filename) {
    ParseResult pr = parse(text, filename);
    Program p; p.ast = std::move(pr.ast); p.diagnostics = std::move(pr.diagnostics);
    Sema s(p); s.run();
    std::stable_sort(p.diagnostics.begin(), p.diagnostics.end(), [](const Diagnostic& a, const Diagnostic& b) {
        auto ka = std::pair(a.error.span ? a.error.span->line : 0u, a.error.span ? a.error.span->column : 0u);
        auto kb = std::pair(b.error.span ? b.error.span->line : 0u, b.error.span ? b.error.span->column : 0u);
        return ka < kb; });
    return p;
}

Result<Program> parseProgram(std::string_view text, std::string filename) {
    Program p = analyzeProgram(text, std::move(filename));
    if (p.ok()) return p;
    // The first error carries every further error as a note.
    std::optional<Error> first;
    for (auto& d : p.diagnostics) {
        if (!d.isError()) continue;
        if (!first) first = d.error;
        else first->notes.push_back(d.error.format());
    }
    return std::unexpected(std::move(*first));
}

} // namespace qlab::lang
