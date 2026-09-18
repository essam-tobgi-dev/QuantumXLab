// Spec 17 §5 — the `device.*` live-value provider and the Illustrative pulse-packet binding of
// spec 17 §8 (see Live.hpp).
//
// Spec 00 §6: `bloch`, `purity` and the excited population read off the state are Simulator-only.
// While the Physical-lab toggle is on they return `nullopt`, which is what makes the Bloch marker
// above a transmon pad and the spec-sheet row disappear rather than show a number no experiment
// could produce; `pop_e` then falls back to the calibrated thermal population (class Model).
#include "App/Live.hpp"
#include "Instruments/ReadoutChain.hpp"
#include <cmath>

namespace qlab::app {
namespace {

using data::FidelityClass;
using detail::PathHead;
using detail::splitPath;
using lab::BindingValue;

BindingValue num(double v, std::string unit, FidelityClass cls) { return BindingValue::number(v, std::move(unit), cls); }

BindingValue simOnly(double v, std::string unit, FidelityClass cls) {
    BindingValue b = BindingValue::number(v, std::move(unit), cls);
    b.simulatorOnly = true;
    return b;
}

// Bloch component / purity / P(|1⟩) of a qubit from the reductions computed for the snapshot.
std::optional<BindingValue> stateValue(const LiveState& s, std::uint32_t qubit, std::string_view rest) {
    if (s.physicalLab) return std::nullopt;
    const viz::SingleReduction* r = s.single(qubit);
    if (r == nullptr) return std::nullopt;
    const FidelityClass cls = s.reductions ? s.reductions->cls : FidelityClass::Exact;
    const PathHead head = splitPath(rest);
    if (head.name == "bloch") {
        const std::array<double, 3> v = r->bloch.array();
        if (!head.index) return simOnly(r->bloch.norm(), {}, cls);
        if (*head.index > 2) return std::nullopt;
        return simOnly(v[*head.index], {}, cls);
    }
    if (rest == "purity") return simOnly(r->purity, {}, cls);
    if (rest == "leakage") return simOnly(r->leakage, {}, cls);
    if (rest == "entropy") return simOnly(r->entropyBits, "bit", cls);
    if (rest == "pop_e") return simOnly(r->bloch.p1(), {}, cls);
    return std::nullopt;
}

std::optional<BindingValue> qubitValue(const LiveState& s, std::uint32_t q, std::string_view rest) {
    const hw::QubitCal* cal = s.calibration ? s.calibration->qubit(q) : nullptr;
    if (auto v = stateValue(s, q, rest)) return v;
    if (cal == nullptr) return std::nullopt;
    if (rest == "f01") return num(cal->f01.value.v, "Hz", FidelityClass::Numerical);
    if (rest == "alpha") return num(cal->anharmonicity.value.v, "Hz", FidelityClass::Numerical);
    if (rest == "T1") return num(cal->t1.value.v, "s", FidelityClass::Numerical);
    if (rest == "T2") return num(cal->t2echo.value.v, "s", FidelityClass::Numerical);
    if (rest == "T2star") return num(cal->t2star.value.v, "s", FidelityClass::Numerical);
    // Spec 00 §6: without the state, the excited population is the calibrated thermal one.
    if (rest == "pop_e") return num(cal->thermalPopulation.value, {}, FidelityClass::Numerical);
    if (rest == "EC" || rest == "EJ") {
        // T05 (3.4) inverted on the exact charge-basis spectrum: (f01, α) → (E_J, E_C)/h.
        auto pair = hw::transmon::fromTargets(cal->f01.value, cal->anharmonicity.value);
        if (!pair) return std::nullopt;
        return num(rest == "EC" ? pair->EC.v : pair->EJ.v, "Hz", FidelityClass::Model);
    }
    if (rest == "readout_fidelity") return num(cal->readoutFidelity(), {}, FidelityClass::Numerical);
    // `phi_ext` and `drive_amp` need a flux bias and a played pulse; neither exists without a
    // pulse-level run, and a fabricated number is worse than "—" (spec 00 §5).
    return std::nullopt;
}

std::optional<BindingValue> resonatorValue(const LiveState& s, std::uint32_t q, std::string_view rest) {
    const hw::QubitCal* cal = s.calibration ? s.calibration->qubit(q) : nullptr;
    if (rest == "f_r") {
        auto f = s.resonatorHz(q);
        return f ? std::optional(num(*f, "Hz", FidelityClass::Numerical)) : std::nullopt;
    }
    if (cal == nullptr) return std::nullopt;
    if (rest == "chi" && cal->readoutChi) return num(cal->readoutChi->value.v, "Hz", FidelityClass::Numerical);
    if (rest == "kappa" && cal->readoutKappa) return num(cal->readoutKappa->value.v, "Hz", FidelityClass::Numerical);
    if (rest == "n_photons") {
        // T05 (6.5): the steady-state occupation of the calibrated tone, zero while no tone is on.
        if (!s.readoutActive || !s.environment) return num(0.0, {}, FidelityClass::Model);
        auto p = instr::readoutFromEnvironment(*s.environment, q);
        if (!p) return std::nullopt;
        return num(p->photons, {}, FidelityClass::Model);
    }
    return std::nullopt;
}

std::optional<BindingValue> edgeValue(const LiveState& s, std::uint32_t e, std::string_view rest) {
    if (!s.device || !s.calibration || e >= s.device->edges.size()) return std::nullopt;
    const hw::EdgeInfo& edge = s.device->edges[e];
    const hw::EdgeCal* cal = s.calibration->edge(edge.a, edge.b);
    if (cal == nullptr) return std::nullopt;
    if (rest == "zz") return num(cal->zz.value.v, "Hz", FidelityClass::Numerical);
    // The calibration measures one exchange coupling per edge; a fixed coupler calls it J and a
    // tunable one its effective value at the operating point. Both read the same number.
    if (rest == "J") return num(cal->couplingG.value.v, "Hz", FidelityClass::Numerical);
    if (rest == "g_eff") return num(cal->couplingG.value.v, "Hz", FidelityClass::Model);
    if (rest == "error") return num(cal->gateError2q.value, {}, FidelityClass::Numerical);
    if (rest == "duration") return num(cal->duration.value.v, "s", FidelityClass::Numerical);
    if (rest == "f_c") {
        if (!edge.coupler) return std::nullopt;
        const hw::QubitCal* c = s.calibration->qubit(*edge.coupler);
        return c ? std::optional(num(c->f01.value.v, "Hz", FidelityClass::Numerical)) : std::nullopt;
    }
    return std::nullopt;
}

} // namespace

lab::BindingRegistry::Provider deviceProvider(std::shared_ptr<const LiveState> state) {
    return [state](std::string_view path) -> std::optional<BindingValue> {
        const LiveState& s = *state;
        const PathHead head = splitPath(path);
        if (path == "T_sample") {
            const cryo::StageArray T = s.stageTemperatures();
            return num(T[static_cast<std::size_t>(cryo::stageIndex(cryo::Stage::MXC))], "K", FidelityClass::Numerical);
        }
        if (head.name == "qubit" && head.index) return qubitValue(s, *head.index, head.rest);
        if (head.name == "res" && head.index) return resonatorValue(s, *head.index, head.rest);
        if (head.name == "edge" && head.index) return edgeValue(s, *head.index, head.rest);
        if (head.name == "pf" && head.index) { // Purcell filter, one per feedline, at the band centre
            auto f = s.resonatorHz(*head.index);
            if (head.rest == "f" && f) return num(*f, "Hz", FidelityClass::Model);
            return std::nullopt;
        }
        if (head.name == "ion" && head.index) {
            if (head.rest == "bright") { // fluorescence: the bright state is |1⟩ (spec 09 §4.5)
                auto p = stateValue(s, *head.index, "pop_e");
                return p;
            }
            return stateValue(s, *head.index, head.rest);
        }
        if (head.name == "trap" && head.rest == "omega_z") {
            if (!s.device || !s.device->motionalModes) return std::nullopt;
            return num(s.device->motionalModes->omegaZ.v, "Hz", FidelityClass::Model);
        }
        return std::nullopt;
    };
}

namespace detail {

std::optional<lab::BindingValue> packetBinding(const LiveState& state, std::string_view path) {
    const PathHead head = splitPath(path);
    if (head.name != "line" || !head.index) return std::nullopt;
    const cryo::WiringLine* line = state.line(*head.index);
    if (line == nullptr || !state.running) return std::nullopt;
    // Spec 17 §8: an Illustrative packet, launched once per repetition of the program. Without a
    // pulse schedule the repetition is the estimator's per-shot time (spec 15 §6).
    const double period = state.result && state.result->estimate.wallTime.perShotS > 0.0
                              ? state.result->estimate.wallTime.perShotS
                              : 1e-6;
    const double t = std::fmod(state.labTimeS, period);
    if (path.ends_with(".t_s")) return BindingValue::number(t, "s", FidelityClass::Illustrative);
    if (path.ends_with(".amp")) {
        const double p = lineInputPower(state, *line);
        return BindingValue::number(p > 0.0 ? 1.0 : 0.6, {}, FidelityClass::Illustrative);
    }
    if (path.ends_with(".sigma_s")) return BindingValue::number(0.2 * period, "s", FidelityClass::Illustrative);
    return std::nullopt;
}

} // namespace detail

} // namespace qlab::app
