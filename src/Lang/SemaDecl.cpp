#include "Lang/Sema.hpp"
#include "Lang/SemaUtil.hpp"
#include <algorithm>
#include <cmath>

// Spec 13 §3, §5, §6 — declarations, gate/subroutine definitions and calibration blocks.
namespace qlab::lang {

namespace {
bool isIntegral(const ConstValue& v) {
    return v.kind != ConstValue::Float || v.f == std::floor(v.f);
}
} // namespace

void Sema::visitQubitDecl(const QubitDecl& d, const SourceSpan& sp) {
    if (sawQuantum_)
        diag("QL3001", sp, d.name);
    Symbol s;
    s.name = d.name;
    s.kind = SymbolKind::QubitReg;
    s.type.base = BaseType::Qubit;
    s.span = sp;
    std::uint32_t n = 1;
    if (d.size) {
        checkExpr(**d.size);
        // An invalid size leaves the register size unknown (0): later index checks stay silent.
        n = 0;
        if (detail::containsErrorExpr(**d.size)) {
        } else if (auto v = fold(**d.size); !v || !v->isNumeric())
            diag("QL3040", sp, "qubit register size");
        else if (!isIntegral(*v) || v->i < 1 || v->i > 4096)
            diag("QL3020", sp, "qubit register size must be an integer in [1, 4096]");
        else
            n = static_cast<std::uint32_t>(v->i);
        s.type.isRegister = true;
        s.type.size = n;
    }
    s.firstQubit = nextQubit_;
    if (declare(s)) {
        p_.qubitRegs.push_back({d.name, nextQubit_, n, sp});
        nextQubit_ += n;
        p_.qubitCount = nextQubit_;
    }
}

void Sema::visitDecl(const ClassicalDecl& d, const SourceSpan& sp) {
    Symbol s;
    s.name = d.name;
    s.span = sp;
    s.type.base = d.type.base;
    if (d.type.base == BaseType::Complex)
        diag("QL3050", sp);
    if (d.type.base == BaseType::Stretch)
        diag("QL3096", sp);
    if (d.type.width) {
        checkExpr(**d.type.width);
        const bool bitRegister = d.type.base == BaseType::Bit;
        if (bitRegister) {
            s.type.isRegister = true;
            s.type.size = 0;
        } // unknown until validated
        if (detail::containsErrorExpr(**d.type.width)) {
        } else if (auto v = fold(**d.type.width); !v || !v->isNumeric())
            diag("QL3040", sp, "type width");
        else if (!isIntegral(*v) || v->i < 1)
            diag("QL3020", sp, "type width must be a positive integer");
        else if (bitRegister)
            s.type.size = static_cast<std::size_t>(v->i);
        else if (d.type.base == BaseType::Float && v->i != 32 && v->i != 64)
            diag("QL3020", sp, "float width must be 32 or 64");
        else
            s.type.width = static_cast<int>(std::min<std::int64_t>(v->i, 1 << 16));
    }
    switch (d.io) {
    case IoKind::Const:
        s.kind = SymbolKind::Const;
        break;
    case IoKind::Input:
        s.kind = SymbolKind::Input;
        break;
    case IoKind::Output:
        s.kind = SymbolKind::Output;
        break;
    case IoKind::None:
        s.kind = SymbolKind::Classical;
        break;
    }
    const bool initMalformed = d.init && detail::containsErrorExpr(**d.init);
    if (d.init) {
        checkExpr(**d.init);
        if (d.init.value()->is<MeasureExpr>()) {
            if (s.type.base != BaseType::Bit)
                diag("QL3020", sp, "measurement result assigned to " + typeName(s.type));
            measuredBits_.insert(d.name);
        } else {
            SemaType it = typeOf(**d.init);
            if (it.base == BaseType::Qubit)
                diag("QL3020", sp, "cannot initialise from a qubit");
            if (s.type.base == BaseType::Duration && it.base != BaseType::Duration &&
                it.base != BaseType::Void)
                diag("QL3020", sp, "duration initialised from " + typeName(it));
        }
        auto v = fold(**d.init);
        if (v && (s.kind == SymbolKind::Const || s.kind == SymbolKind::Input))
            s.value = v;
        if (s.kind == SymbolKind::Const && !v && !initMalformed)
            diag("QL3040", sp, "const initialiser");
        if (dependsOnMeasurement(**d.init))
            measuredBits_.insert(d.name);
    } else if (s.kind == SymbolKind::Const)
        diag("QL3040", sp, "const initialiser");
    if (s.kind == SymbolKind::Input) {
        for (auto& sw : p_.pragmas.sweeps)
            if (sw.input == d.name)
                s.hasSweep = true;
        p_.inputs.push_back({d.name, s.type, s.value, s.hasSweep, sp});
        if (!s.hasSweep && !s.value && !initMalformed)
            diag("QL3178", sp, d.name);
    }
    if (s.kind == SymbolKind::Output)
        p_.outputs.push_back(d.name);
    if (declare(s) && s.type.base == BaseType::Bit && scopes_.size() == 1) {
        const auto bits = static_cast<std::uint32_t>(s.type.isRegister ? s.type.size : 1);
        p_.bitRegs.push_back({d.name, bits, sp});
        p_.bitCount += bits;
    }
}

void Sema::visitGateDecl(const GateDecl& g, const SourceSpan& sp) {
    Symbol s;
    s.name = g.name;
    s.kind = SymbolKind::Gate;
    s.span = sp;
    s.nParams = static_cast<int>(g.params.size());
    s.nQubits = static_cast<int>(g.qubits.size());
    // Spec 13 §3–§4: stdgates and built-in gate names cannot be redefined; native device gates
    // (ecr, rzz, …) are not stdgates and may be. A rejected definition still declares its name
    // (body unchecked) so that its uses resolve instead of cascading into QL3010.
    if (const GateInfo* lib = StdGates::find(g.name); lib && lib->kind != GateKind::Native) {
        diag("QL3061", sp, g.name);
        declare(std::move(s));
        return;
    }
    if (!declare(s))
        return;
    p_.userGates[g.name] = &g;
    pushScope(true, true, false);
    for (auto& pn : g.params) {
        Symbol ps;
        ps.name = pn;
        ps.kind = SymbolKind::GateParam;
        ps.type.base = BaseType::Angle;
        ps.span = sp;
        declare(ps);
    }
    for (auto& qn : g.qubits) {
        Symbol qs;
        qs.name = qn;
        qs.kind = SymbolKind::GateQubit;
        qs.type.base = BaseType::Qubit;
        qs.span = sp;
        declare(qs);
    }
    std::string saved = currentCallable_;
    currentCallable_ = g.name;
    bool savedQ = sawQuantum_;
    for (auto& st : g.body) {
        if (!st)
            continue;
        // The parser already rejects (and drops) declarations, measure, reset, delay and classical
        // statements in gate bodies (QL2012/QL2010); what reaches here is only diagnosed once.
        if (st->is<GateCall>() || st->is<BarrierStmt>())
            visitStmt(*st);
        else
            diag("QL3177", st->span, detail::stmtKindName(*st));
    }
    sawQuantum_ = savedQ;
    currentCallable_ = saved;
    popScope();
}

void Sema::visitDef(const DefStmt& d, const SourceSpan& sp) {
    Symbol s;
    s.name = d.name;
    s.kind = SymbolKind::Def;
    s.span = sp;
    s.type.base = d.returnType.base;
    s.nParams = static_cast<int>(d.params.size());
    if (!declare(s))
        return;
    p_.defs[d.name] = &d;
    pushScope(true, false, true);
    for (auto& p : d.params) {
        Symbol ps;
        ps.name = p.name;
        ps.kind = SymbolKind::DefParam;
        ps.span = sp;
        ps.type.base = p.isQubit ? BaseType::Qubit : p.type.base;
        const std::optional<ExprPtr>& size = p.isQubit ? p.qubitSize : p.type.width;
        if (size && (p.isQubit || p.type.base == BaseType::Bit)) {
            auto v = fold(**size);
            ps.type.isRegister = true;
            ps.type.size = v && v->i > 0 ? static_cast<std::size_t>(v->i) : 0; // 0: unknown size
        } else if (size) {
            if (auto v = fold(**size); v && v->i > 0)
                ps.type.width = static_cast<int>(std::min<std::int64_t>(v->i, 1 << 16));
        }
        declare(ps);
    }
    std::string saved = currentCallable_;
    currentCallable_ = d.name;
    bool savedQ = sawQuantum_, savedM = sawMeasure_;
    sawMeasure_ = false;
    ++defDepth_;
    visitBody(d.body);
    --defDepth_;
    if (sawMeasure_)
        measuringDefs_.insert(d.name);
    sawQuantum_ = savedQ;
    sawMeasure_ = savedM;
    currentCallable_ = saved;
    popScope();
}

void Sema::visitCalBody(const std::vector<PulseStmt>& body, std::set<std::string>& frames) {
    for (auto& ps : body) {
        if (auto* f = std::get_if<PulseFrameDecl>(&ps.node)) {
            frames.insert(f->name);
            checkExpr(*f->frequency);
            checkExpr(*f->phase);
        } else if (auto* w = std::get_if<PulseWaveformDecl>(&ps.node)) {
            for (auto& a : w->waveform.args)
                checkExpr(*a);
            if (!w->waveform.args.empty() && w->waveform.kind != "samples" &&
                w->waveform.kind != "ref") {
                auto v = fold(*w->waveform.args[0]);
                if (v && v->isNumeric() && std::abs(v->asDouble()) > 1.0)
                    diag("QL3120", ps.span, v->asDouble());
            }
        } else if (auto* pl = std::get_if<PulsePlay>(&ps.node)) {
            if (auto* pw = std::get_if<PulseWaveform>(&pl->waveform)) {
                for (auto& a : pw->args)
                    checkExpr(*a);
                if (!pw->args.empty() && pw->kind != "samples") {
                    auto v = fold(*pw->args[0]);
                    if (v && v->isNumeric() && std::abs(v->asDouble()) > 1.0)
                        diag("QL3120", ps.span, v->asDouble());
                }
            }
        } else if (auto* op = std::get_if<PulseFrameOp>(&ps.node))
            checkExpr(*op->value);
        else if (auto* dl = std::get_if<PulseDelay>(&ps.node))
            checkExpr(*dl->duration);
        else if (auto* c = std::get_if<PulseCapture>(&ps.node))
            checkExpr(*c->duration);
    }
}

void Sema::visitDefcal(const DefcalStmt& d, const SourceSpan& sp) {
    if (p_.usedGates.contains(d.gate))
        diag("QL3110", sp, d.gate);
    defcalGates_.emplace(d.gate, &d);
    p_.defcals.push_back(&d);
    pushScope(true, false, true);
    std::string saved = currentCallable_;
    currentCallable_ = "defcal " + d.gate;
    for (std::size_t i = 0; i < d.paramNames.size(); ++i)
        if (!d.paramNames[i].empty()) {
            Symbol s;
            s.name = d.paramNames[i];
            s.kind = SymbolKind::DefParam;
            s.type.base = BaseType::Angle;
            s.span = sp;
            declare(s);
        }
    std::set<std::string> frames;
    visitCalBody(d.body, frames);
    currentCallable_ = saved;
    popScope();
}

} // namespace qlab::lang
