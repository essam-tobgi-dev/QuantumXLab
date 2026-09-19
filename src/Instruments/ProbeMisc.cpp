// Spec 12 §12 — `probe_fidelity`, `probe_trajectory`, `probe_thermal_truth`, `probe_leakage`.
#include "Data/Fidelity.hpp"
#include "Instruments/Probes.hpp"
#include "Instruments/StateAccess.hpp"
#include "Numerics/Numerics.hpp"
#include <cmath>
#include <format>

namespace qlab::instr {

// ---- probe_fidelity -----------------------------------------------------------------------

SettingSchema FidelityProbe::makeSchema() {
    SettingSchema s;
    s.id = "instr/probe_fidelity.schema.json";
    s.instrument = "probe_fidelity";
    s.settings = {SettingSpec::integer("history_points", "", 1, 100000, 4096, "Snapshots kept"),
                  refreshRateSetting()};
    return s;
}

FidelityProbe::FidelityProbe(std::uint32_t index)
    : ProbeBase({"probe_fidelity", index}, makeSchema()) {
    setProbeChannels({
        {{},
         "state_fidelity",
         "",
         "",
         FidelityClass::Exact,
         false,
         true,
         "F = <psi_ideal|rho|psi_ideal> versus gate index"},
        {{},
         "process_fidelity",
         "",
         "",
         FidelityClass::Exact,
         false,
         true,
         "Process fidelity of the selected gate; marker `F_avg`"},
    });
}

Result<double> FidelityProbe::stateFidelity(const qsim::Snapshot& state,
                                            const qsim::Snapshot& ideal) {
    if (state.nQubits != ideal.nQubits || state.levels != ideal.levels)
        return fail(err::BadInput,
                    "probe_fidelity: the run and its ideal reference describe different registers");
    if (ideal.amplitudes) { // squared convention (DEVELOPMENT.md): |⟨φ|ψ⟩|² or ⟨φ|ρ|φ⟩
        if (state.amplitudes) {
            if (state.amplitudes->size() != ideal.amplitudes->size())
                return fail(err::BadInput, "probe_fidelity: state sizes differ");
            return num::fidelity(std::span<const num::Complex>(*ideal.amplitudes),
                                 std::span<const num::Complex>(*state.amplitudes));
        }
        if (state.densityMatrix) {
            if (state.densityMatrix->rows != ideal.amplitudes->size())
                return fail(err::BadInput, "probe_fidelity: state sizes differ");
            return num::fidelity(std::span<const num::Complex>(*ideal.amplitudes),
                                 *state.densityMatrix);
        }
    }
    QXL_TRY_ASSIGN(num::Matrix rho, fullDensity(state));
    QXL_TRY_ASSIGN(num::Matrix sigma, fullDensity(ideal));
    if (rho.rows != sigma.rows)
        return fail(err::BadInput, "probe_fidelity: state sizes differ");
    return num::fidelity(rho, sigma); // Uhlmann, squared
}

Result<double> FidelityProbe::processFidelity(const GateComparison& gate) {
    const std::size_t d = gate.ideal.rows;
    if (d == 0 || gate.ideal.cols != d || gate.kraus.empty())
        return fail(err::BadInput, "probe_fidelity: gate comparison is empty");
    double f = 0.0;
    for (auto const& k : gate.kraus) {
        if (k.rows != d || k.cols != d)
            return fail(err::BadInput,
                        "probe_fidelity: Kraus operator and ideal matrix differ in size");
        f += std::norm(num::trace(num::matmul(num::adjoint(gate.ideal), k)));
    }
    return f / static_cast<double>(d * d);
}

Result<Trace> FidelityProbe::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    Trace t = makeTrace(channel, ctx);
    if (channel.name == "process_fidelity") {
        if (!ctx.run || !ctx.run->gate)
            return fail(err::NotBound, id().toString() + ": no gate is selected for comparison");
        QXL_TRY_ASSIGN(double f, processFidelity(*ctx.run->gate));
        const auto d = static_cast<double>(ctx.run->gate->ideal.rows);
        t.x = {ctx.stamp.labTimeS};
        t.y = {f};
        t.markers.push_back({t.x[0], f, "F_avg", (d * f + 1.0) / (d + 1.0), 0.0, ""}); // T10 §1.3
        t.markers.push_back({t.x[0], f, "gate: " + ctx.run->gate->name, f, 0.0, ""});
        return t;
    }
    QXL_TRY_ASSIGN(auto state, stateOf(ctx, id()));
    if (!ctx.run->ideal)
        return fail(err::NotBound,
                    id().toString() + ": the run view holds no ideal reference state");
    QXL_TRY_ASSIGN(double f, stateFidelity(*state, *ctx.run->ideal));
    if (!history_.empty() && state->gateIndex < history_.back().first)
        history_.clear(); // a new run
    if (history_.empty() || history_.back().first != state->gateIndex)
        history_.push_back({state->gateIndex, f});
    else
        history_.back().second = f;
    const auto keep = static_cast<std::size_t>(ctx.settings.integer("history_points"));
    while (history_.size() > keep)
        history_.pop_front();
    t.cls = data::weakest(state->cls, ctx.run->ideal->cls);
    for (auto const& [gate, value] : history_) {
        t.x.push_back(static_cast<double>(gate));
        t.y.push_back(value);
    }
    t.markers.push_back({t.x.back(), f, "F", f, 0.0, ""});
    return t;
}

// ---- probe_trajectory ---------------------------------------------------------------------

SettingSchema TrajectoryProbe::makeSchema() {
    SettingSchema s;
    s.id = "instr/probe_trajectory.schema.json";
    s.instrument = "probe_trajectory";
    s.settings = {refreshRateSetting()};
    return s;
}

TrajectoryProbe::TrajectoryProbe(std::uint32_t index)
    : ProbeBase({"probe_trajectory", index}, makeSchema()) {
    setProbeChannels(
        {{{},
          "jumps",
          "s",
          "",
          FidelityClass::Statistical,
          false,
          true,
          "Jump record of the current trajectory: x = time, y = collapse-operator index"}});
}

Result<Trace> TrajectoryProbe::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    if (!ctx.run)
        return fail(err::NotBound, id().toString() + ": no run is bound");
    Trace t = makeTrace(channel, ctx);
    for (auto const& j : ctx.run->jumps) {
        t.x.push_back(j.timeS);
        t.y.push_back(static_cast<double>(j.op));
        t.markers.push_back(
            {j.timeS, static_cast<double>(j.op), j.name, static_cast<double>(j.op), 0.0, ""});
    }
    if (ctx.run->backend != qsim::Kind::Trajectories)
        t.markers.push_back({0.0, 0.0, "not a Monte-Carlo run", 0.0, 0.0, ""});
    return t;
}

// ---- probe_thermal_truth ------------------------------------------------------------------

SettingSchema ThermalTruthProbe::makeSchema() {
    SettingSchema s;
    s.id = "instr/probe_thermal_truth.schema.json";
    s.instrument = "probe_thermal_truth";
    s.settings = {refreshRateSetting()};
    return s;
}

ThermalTruthProbe::ThermalTruthProbe(std::uint32_t index)
    : ProbeBase({"probe_thermal_truth", index}, makeSchema()) {
    setProbeChannels({{{},
                       "stages",
                       "",
                       "K",
                       FidelityClass::Numerical,
                       false,
                       true,
                       "True stage temperatures: x = stage index (RT … MXC); aux `reading` = "
                       "thermometer on that stage"}});
}

std::optional<double> ThermalTruthProbe::query(std::string_view path) const {
    auto env = environment();
    if (!env || !path.starts_with("stage.") || !path.ends_with(".T"))
        return InstrumentBase::query(path);
    cryo::Stage stage = cryo::Stage::MXC;
    if (!cryo::stageFromName(path.substr(6, path.size() - 8), stage))
        return std::nullopt;
    return env->stageTemperatures()[static_cast<std::size_t>(cryo::stageIndex(stage))];
}

Result<Trace> ThermalTruthProbe::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    if (!ctx.env)
        return fail(err::NotBound,
                    id().toString() + ": no environment (thermal snapshot) is bound");
    Trace t = makeTrace(channel, ctx);
    const cryo::StageArray truth = ctx.env->stageTemperatures();
    auto& reading = t.aux["reading"];
    for (cryo::Stage stage : cryo::kStages) {
        const auto i = static_cast<std::size_t>(cryo::stageIndex(stage));
        t.x.push_back(static_cast<double>(i));
        t.y.push_back(truth[i]);
        double shown = std::nan("");
        for (const Thermometer* th : thermometers_)
            if (th && th->stage() == stage)
                if (auto last = th->lastReading())
                    shown = last->temperatureK;
        reading.push_back(shown);
        t.markers.push_back({static_cast<double>(i), truth[i], std::string(cryo::stageName(stage)),
                             truth[i], 0.0, "K"});
    }
    return t;
}

// ---- probe_leakage ------------------------------------------------------------------------

SettingSchema LeakageProbe::makeSchema() {
    SettingSchema s;
    s.id = "instr/probe_leakage.schema.json";
    s.instrument = "probe_leakage";
    s.settings = {refreshRateSetting()};
    return s;
}

LeakageProbe::LeakageProbe(std::uint32_t index)
    : ProbeBase({"probe_leakage", index}, makeSchema()) {
    setProbeChannels({{{},
                       "leakage",
                       "s",
                       "",
                       FidelityClass::Numerical,
                       false,
                       true,
                       "Population outside the computational subspace versus time: y = all qubits, "
                       "aux `q<k>` per qubit"}});
}

Result<Trace> LeakageProbe::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    if (!ctx.run)
        return fail(err::NotBound, id().toString() + ": no run is bound");
    const RunView& run = *ctx.run;
    Trace t = makeTrace(channel, ctx);
    if (run.backend == qsim::Kind::Trajectories)
        t.cls = FidelityClass::Statistical;
    const std::size_t levels = run.levels, sites = run.nQubits;
    for (auto const& sample : run.populations) {
        if (sample.populations.size() != levels * sites)
            return fail(
                err::BadInput,
                std::format("{}: a population sample has {} entries, expected {} sites x {} levels",
                            id().toString(), sample.populations.size(), sites, levels));
        t.x.push_back(sample.timeS);
        double total = 0.0;
        for (std::size_t q = 0; q < sites; ++q) {
            double leaked = 0.0;
            for (std::size_t l = 2; l < levels; ++l)
                leaked += sample.populations[q * levels + l];
            t.aux[std::format("q{}", q)].push_back(leaked);
            total += leaked;
        }
        t.y.push_back(total);
    }
    if (levels < 3)
        t.markers.push_back({0.0, 0.0, "two-level run: no leakage levels", 0.0, 0.0, ""});
    return t;
}

} // namespace qlab::instr
