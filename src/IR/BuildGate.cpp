// Spec 14 §2 — gate calls, modifiers (ctrl, negctrl, inv, pow), user gate expansion, def inlining.
#include "IR/BuildImpl.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace qlab::ir::build {

Status Builder::lowerGateCall(const lang::GateCall& c, const Applied& outer) {
    // `f(q[0]);` of a void subroutine parses as a gate call whose arguments sit in `params`.
    if (auto d = p_.defs.find(c.name);
        d != p_.defs.end() && c.modifiers.empty() && c.qubits.empty()) {
        QXL_TRY(callDef(*d->second, c.params));
        return {};
    }
    std::vector<double> params;
    for (const auto& e : c.params) {
        QXL_TRY_ASSIGN(double v, evalDouble(*e));
        params.push_back(v);
    }
    std::vector<std::vector<Wire>> operands;
    for (const auto& q : c.qubits) {
        QXL_TRY_ASSIGN(auto ws, qubitOperand(*q));
        if (ws.empty())
            return fail(err::BadWire, std::format("gate '{}' has an empty operand", c.name));
        operands.push_back(std::move(ws));
    }
    // Spec 13 §3: modifiers compose left to right; ctrl(n)/negctrl(n) take the next n operands.
    std::vector<std::uint8_t> controlKinds;
    double power = 1.0;
    for (const auto& m : c.modifiers) {
        Value arg = Value::ofInt(1);
        if (m.arg) {
            QXL_TRY_ASSIGN(arg, eval(**m.arg));
        }
        if (!arg.constant() || arg.kind == Value::Kind::Dur)
            return fail(
                err::NotConstant,
                std::format("modifier argument of '{}' must be a number known at build time",
                            c.name));
        switch (m.kind) {
        case lang::ModifierKind::Ctrl:
        case lang::ModifierKind::NegCtrl:
            if (arg.asInt() < 1)
                return fail(err::BadArity, "ctrl(n) and negctrl(n) need n >= 1");
            controlKinds.insert(controlKinds.end(), static_cast<std::size_t>(arg.asInt()),
                                m.kind == lang::ModifierKind::NegCtrl ? 1 : 0);
            break;
        case lang::ModifierKind::Inv:
            power = -power;
            break;
        case lang::ModifierKind::Pow:
            power *= arg.asDouble();
            break;
        }
    }
    if (controlKinds.size() > operands.size())
        return fail(err::BadArity, std::format("gate '{}' has {} control(s) but {} operand(s)",
                                               c.name, controlKinds.size(), operands.size()));
    // Broadcast (spec 13 §3): register operands of equal size apply the gate element-wise.
    std::size_t slices = 1;
    for (const auto& o : operands)
        if (o.size() > 1) {
            if (slices > 1 && o.size() != slices)
                return fail(err::BadArity, "register operands of different sizes in one gate call");
            slices = o.size();
        }
    const bool opaque = p_.pulseOnlyGates.contains(c.name);
    const auto user = p_.userGates.find(c.name);
    for (std::size_t i = 0; i < slices; ++i) {
        Applied a = outer;
        a.power *= power;
        std::vector<Wire> targets;
        for (std::size_t j = 0; j < operands.size(); ++j) {
            const Wire w = operands[j].size() == 1 ? operands[j][0] : operands[j][i];
            if (j < controlKinds.size()) {
                a.controls.push_back(w);
                a.negControl.push_back(controlKinds[j]);
            } else
                targets.push_back(w);
        }
        if (user != p_.userGates.end() && !opaque)
            QXL_TRY(expandUserGate(*user->second, params, targets, a));
        else
            QXL_TRY(applyGate(c.name, params, targets, a, opaque));
    }
    return {};
}

Status Builder::applyGate(std::string_view name, const std::vector<double>& params,
                          const std::vector<Wire>& targets, const Applied& mod, bool opaque) {
    const GateDef* def = opaque ? nullptr : gates::find(name);
    if (!opaque) {
        if (!def || name == "unitary")
            return fail(err::UnknownGate, std::format("unknown gate '{}'", name));
        if (static_cast<int>(params.size()) != def->nParams)
            return fail(err::BadArity, std::format("gate '{}' takes {} parameter(s), got {}", name,
                                                   def->nParams, params.size()));
        if (static_cast<int>(targets.size()) != def->nQubits)
            return fail(err::BadArity, std::format("gate '{}' acts on {} qubit(s), got {}", name,
                                                   def->nQubits, targets.size()));
    }
    Gate base;
    base.name = std::string(name);
    base.params = params;
    base.targets = targets;
    base.opaque = opaque;
    const auto rewrite = [&](const gates::GateRewrite& rw) {
        base.name = rw.name;
        base.params = rw.params;
    };
    // Spec 14 §2 modifiers: pow scales rotation angles, uses a named closed form, or repeats the
    // gate; inv takes the named inverse or marks the node adjoint. No matrix is formed here.
    const double k = std::nearbyint(mod.power);
    std::uint64_t repeats = 1;
    if (std::abs(mod.power - k) > 1e-12) {
        auto rw = def && def->rotation ? gates::powerOf(name, params, mod.power) : std::nullopt;
        if (!rw)
            return fail(
                err::Unsupported,
                std::format("pow({}) @ {}: only rotation gates take non-integer powers (QL3070)",
                            mod.power, name));
        rewrite(*rw);
    } else if (k == 0) {
        return {};
    } else if (auto named = def ? gates::namedPower(name, params, k) : std::nullopt) {
        if (named->name.empty())
            return {}; // exactly the identity, also under control
        rewrite(*named);
    } else if (def && def->rotation) {
        rewrite(*gates::powerOf(name, params, k));
    } else {
        if (k < 0) {
            if (auto inv = def ? gates::inverseOf(name, params) : std::nullopt)
                rewrite(*inv);
            else
                base.adjoint = true;
        }
        repeats = static_cast<std::uint64_t>(std::abs(k));
        if (repeats > opts_.loopUnrollBound)
            return fail(err::Unsupported,
                        std::format("pow({}) @ {} repeats the gate beyond the unroll bound {}", k,
                                    name, opts_.loopUnrollBound));
    }
    for (std::uint64_t r = 0; r < repeats; ++r)
        QXL_TRY(emitGate(base, mod));
    return {};
}

Status Builder::emitGate(Gate g, const Applied& mod) {
    std::vector<Wire> controls = mod.controls;
    std::vector<std::uint8_t> neg = mod.negControl;
    if (!controls.empty() && !g.opaque && !g.adjoint) {
        // Spec 13 §3: under control, gphase(γ) is a phase gate on the control.
        if (g.name == "gphase")
            for (std::size_t j = controls.size(); j-- > 0;)
                if (neg[j] == 0) {
                    g.name = "p";
                    g.targets = {controls[j]};
                    controls.erase(controls.begin() + static_cast<std::ptrdiff_t>(j));
                    neg.erase(neg.begin() + static_cast<std::ptrdiff_t>(j));
                    break;
                }
        // Canonical form: split a named controlled gate into base + its (innermost) controls, then
        // join all-positive controls back into a named gate where one exists (ctrl @ cx → ccx).
        if (auto split = gates::splitControlled(g.name, g.params)) {
            for (std::size_t j = 0; j < split->controls; ++j) {
                controls.push_back(g.targets[j]);
                neg.push_back(0);
            }
            g.targets.erase(g.targets.begin(),
                            g.targets.begin() + static_cast<std::ptrdiff_t>(split->controls));
            g.name = split->base.name;
            g.params = split->base.params;
        }
    }
    const bool allPositive =
        std::none_of(neg.begin(), neg.end(), [](std::uint8_t v) { return v != 0; });
    if (!controls.empty() && allPositive && !g.opaque && !g.adjoint)
        if (auto joined = gates::joinControlled(g.name, g.params, controls.size())) {
            g.targets.insert(g.targets.begin(), controls.begin(), controls.end());
            controls.clear();
            g.name = joined->name;
            g.params = joined->params;
        }
    g.controls = std::move(controls);
    if (!allPositive)
        g.negControl = std::move(neg);
    g.cls = g.opaque || g.adjoint ? GateClass::Generic : gates::classify(g.name, g.params);
    std::set<std::uint32_t> seen;
    for (Wire w : g.wires())
        if (!seen.insert(w.index).second)
            return fail(
                err::BadWire,
                std::format(
                    "gate '{}' uses wire {} more than once (controls and targets must be disjoint)",
                    g.name, w.index));
    emit(std::move(g));
    return {};
}

Status Builder::expandUserGate(const lang::GateDecl& g, const std::vector<double>& params,
                               const std::vector<Wire>& qubits, const Applied& mod) {
    if (params.size() != g.params.size() || qubits.size() != g.qubits.size())
        return fail(err::BadArity, std::format("gate '{}' takes {} parameter(s) and {} qubit(s)",
                                               g.name, g.params.size(), g.qubits.size()));
    const double k = std::nearbyint(mod.power);
    if (std::abs(mod.power - k) > 1e-12)
        return fail(err::Unsupported,
                    std::format("pow({}) @ {}: a user gate takes only integer powers (QL3070)",
                                mod.power, g.name));
    if (k == 0)
        return {};
    if (inlineDepth_ >= opts_.maxInlineDepth)
        return fail(err::Unsupported, std::format("gate '{}' expands deeper than {} levels", g.name,
                                                  opts_.maxInlineDepth));
    const auto repeats = static_cast<std::uint64_t>(std::abs(k));
    if (repeats > opts_.loopUnrollBound)
        return fail(
            err::Unsupported,
            std::format("pow({}) @ {} repeats the body beyond the unroll bound", k, g.name));
    // pow(k) repeats the body; inv reverses it and inverts every call (spec 14 §2). Controls reach
    // every gate of the body, so gphase inside a controlled body becomes a phase on the control.
    Applied inner = mod;
    inner.power = k < 0 ? -1.0 : 1.0;
    ++inlineDepth_;
    ++spanPin_;
    Status st;
    for (std::uint64_t r = 0; r < repeats && st; ++r) {
        push();
        for (std::size_t i = 0; i < params.size(); ++i) {
            Binding b;
            b.value = Value::ofFloat(params[i]);
            bind(g.params[i], std::move(b));
        }
        for (std::size_t i = 0; i < qubits.size(); ++i) {
            Binding b;
            b.kind = Binding::Kind::Qubits;
            b.qubits = {qubits[i]};
            bind(g.qubits[i], std::move(b));
        }
        const std::size_t n = g.body.size();
        for (std::size_t s = 0; s < n && st; ++s) {
            const auto& stmt = g.body[k < 0 ? n - 1 - s : s];
            if (!stmt)
                continue;
            if (const auto* call = stmt->as<lang::GateCall>()) {
                st = lowerGateCall(*call, inner);
            } else if (const auto* bar = stmt->as<lang::BarrierStmt>()) {
                Barrier b;
                for (const auto& q : bar->qubits) {
                    auto ws = qubitOperand(*q);
                    if (!ws) {
                        st = std::unexpected(ws.error());
                        break;
                    }
                    b.wires.insert(b.wires.end(), ws->begin(), ws->end());
                }
                if (bar->qubits.empty())
                    b.wires = qubits; // `barrier;` spans the gate's own qubits
                if (st)
                    emit(std::move(b));
            }
        }
        pop();
    }
    --spanPin_;
    --inlineDepth_;
    return st;
}

Result<Value> Builder::callDef(const lang::DefStmt& d, std::span<const lang::ExprPtr> args) {
    if (args.size() != d.params.size())
        return fail(err::BadArity, std::format("subroutine '{}' takes {} argument(s), got {}",
                                               d.name, d.params.size(), args.size()));
    if (inlineDepth_ >= opts_.maxInlineDepth)
        return fail(err::Unsupported, std::format("subroutine '{}' inlines deeper than {} levels",
                                                  d.name, opts_.maxInlineDepth));
    // Arguments are evaluated at the call site: qubits by reference, classical values by value.
    std::vector<Binding> bound(args.size());
    for (std::size_t i = 0; i < args.size(); ++i) {
        const lang::DefParam& prm = d.params[i];
        if (prm.isQubit) {
            bound[i].kind = Binding::Kind::Qubits;
            QXL_TRY_ASSIGN(bound[i].qubits, qubitOperand(*args[i]));
            std::int64_t want = 1;
            if (prm.qubitSize) {
                QXL_TRY_ASSIGN(Value n, eval(**prm.qubitSize));
                want = n.asInt();
            }
            if (static_cast<std::int64_t>(bound[i].qubits.size()) != want)
                return fail(err::BadArity, std::format("argument '{}' of '{}' needs {} qubit(s)",
                                                       prm.name, d.name, want));
        } else {
            QXL_TRY_ASSIGN(bound[i].value, eval(*args[i]));
        }
    }
    ++inlineDepth_;
    ++spanPin_;
    push();
    for (std::size_t i = 0; i < args.size(); ++i)
        bind(d.params[i].name, std::move(bound[i]));
    std::optional<Value> outerReturn = std::move(returned_);
    returned_.reset();
    Status st = lowerBody(d.body);
    Value result = returned_ ? std::move(*returned_) : Value::ofInt(0);
    returned_ = std::move(outerReturn);
    if (flow_ == Flow::Return)
        flow_ = Flow::Normal;
    pop();
    --spanPin_;
    --inlineDepth_;
    QXL_TRY(st);
    return result;
}

} // namespace qlab::ir::build
