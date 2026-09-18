// Spec 15 §3.5 (c) — the pulse-level execution model: one Lindblad evolution of the compiled
// schedule, per-line drive power published to `cryo` at 10 Hz while it runs, and the calibrated
// readout POVM applied to the final ρ.
#include "Runtime/Engine.hpp"
#include "Data/Fidelity.hpp"
#include "Runtime/PulseModel.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

namespace qlab::runtime {
namespace {

// Full-space index whose qubit sites hold the bits of `word` and whose extra sites hold `extra`.
std::size_t fullIndex(std::span<const std::uint32_t> dims, std::uint32_t qubitSites, std::size_t word,
                      std::span<const std::uint32_t> extra) {
    std::size_t index = 0, stride = 1;
    for (std::size_t s = 0; s < dims.size(); ++s) {
        const std::uint32_t level =
            s < qubitSites ? static_cast<std::uint32_t>((word >> s) & 1) : extra[s - qubitSites];
        index += static_cast<std::size_t>(level) * stride;
        stride *= dims[s];
    }
    return index;
}

// One over-the-extra-sites odometer step; false when the enumeration is done.
bool advance(std::span<std::uint32_t> extra, std::span<const std::uint32_t> dims, std::uint32_t qubitSites) {
    for (std::size_t k = 0; k < extra.size(); ++k) {
        if (++extra[k] < dims[qubitSites + k]) return true;
        extra[k] = 0;
    }
    return false;
}

// T12 §7 / spec 07 §5: the integrator step that keeps the fastest phase in the model below
// `kPhasePerStep` radians, so the RK4 global error stays far below the fidelities we report.
constexpr double kPhasePerStep = 0.05;
double automaticStep(const qsim::SystemModel& m, double durationS, double sampleS) {
    double bound = num::norm1(m.h0);
    constexpr int kProbes = 512;
    for (int k = 0; k <= kProbes; ++k) {
        const double t = durationS * static_cast<double>(k) / kProbes;
        double drive = 0.0;
        for (const qsim::DriveTerm& d : m.drives) {
            if (!d.envelope) continue;
            const num::Complex omega = d.envelope(t);
            drive += std::abs(omega.real()) * num::norm1(d.inPhase) + std::abs(omega.imag()) * num::norm1(d.quadrature);
        }
        bound = std::max(bound, num::norm1(m.h0) + drive);
    }
    if (!(bound > 0.0)) return sampleS;
    return std::clamp(kPhasePerStep / bound, 1e-15, sampleS);
}
} // namespace

// ρ of the qubit sites: the motional mode (ions) is traced out and every qubit site is projected on
// {|0⟩, |1⟩}; the result is renormalised, so the discarded weight is the leakage.
num::Matrix computationalRho(const num::Matrix& rho, std::span<const std::uint32_t> dims, std::uint32_t qubitSites) {
    const std::size_t dim = std::size_t{1} << qubitSites;
    num::Matrix out(dim, dim);
    std::vector<std::uint32_t> extra(dims.size() - qubitSites, 0);
    do {
        for (std::size_t a = 0; a < dim; ++a)
            for (std::size_t b = 0; b < dim; ++b)
                out(a, b) += rho(fullIndex(dims, qubitSites, a, extra), fullIndex(dims, qubitSites, b, extra));
    } while (advance(extra, dims, qubitSites));
    double trace = 0.0;
    for (std::size_t a = 0; a < dim; ++a) trace += out(a, a).real();
    if (trace > 1e-12)
        for (auto& v : out.data) v /= trace;
    return out;
}

Result<ExecutionOutput> executePulse(const ExecutionInput& in) {
    if (!in.program || !in.plan) return fail(err::NotCompiled, "executePulse needs a compiled program and its plan");
    if (in.backend != qsim::Kind::Lindblad)
        return fail(err::Unsupported, std::format("the pulse-level model runs on the Lindblad backend, not on {}",
                                                  qsim::kindName(in.backend)));
    static const RunOptions kDefault;
    const RunOptions& options = in.options ? *in.options : kDefault;
    const ProgramPlan& plan = *in.plan;
    QXL_TRY_ASSIGN(const PulseModel pm, buildPulseModel(in, plan));

    qsim::LindbladBackend backend;
    qsim::LindbladSettings settings;
    settings.recordTrajectory = true;
    const double dtS = static_cast<double>(in.program->pulses->schedule.dt().get()) * 1e-12;
    // Record on the AWG grid (spec 07 §5), but never more than `maxSnapshots` × 64 samples: a 200 µs
    // ion gate on a 1 ns grid would otherwise record 2·10⁵ density matrices.
    const double maxSamples = std::max<double>(64.0, options.maxSnapshots * 64.0);
    settings.sampleS = std::max(dtS > 0.0 ? dtS : 1e-9, pm.durationS / maxSamples);
    settings.stepS = options.pulseStepS > 0.0 ? options.pulseStepS
                                              : automaticStep(pm.model, pm.durationS, settings.sampleS);
    backend.setSettings(settings);
    QXL_TRY(backend.setModel(pm.model));

    ExecutionOutput out;
    out.schedule = std::make_shared<const pulse::Schedule>(in.program->pulses->schedule);
    // Spec 11 §5: the power this schedule puts on each fridge line, averaged over the repetition
    // period, is what `cryo` dissipates while the run is active.
    cryo::LinePowers powers;
    if (in.device) {
        const double repetitionS = std::max(pm.durationS, static_cast<double>(in.device->timing.repetitionDelay.v));
        if (auto p = schedulePower(in.program->pulses->schedule, *in.device, repetitionS)) powers = std::move(*p);
        else out.diagnostics.push_back(note("no line power was published to cryo: " + p.error().message,
                                            lang::Severity::Warning));
    }
    const auto publish = [&](double progress) {
        if (!in.events) return;
        LinePowerEvent ev;
        ev.run = in.id;
        ev.device = in.device ? in.device->id : std::string{};
        ev.progress = progress;
        ev.powers = progress >= 1.0 ? cryo::LinePowers{} : powers; // the lines are quiet once the job ends
        in.events->post(std::move(ev));
    };

    // Cooperative cancellation every chunk (spec 15 §3), with a 10 Hz power publication. The chunk
    // grows from one recorded segment until it costs about 20 ms of wall time, so a cancelled run
    // stops well inside the 100 ms budget of spec 24 §4.
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    auto lastPublish = started;
    publish(0.0);
    double chunk = settings.sampleS;
    while (backend.timeS() < pm.durationS - 1e-18) {
        if (in.stop.stop_requested()) {
            out.partial = true;
            break;
        }
        const auto before = Clock::now();
        const double target = std::min(pm.durationS, backend.timeS() + chunk);
        QXL_TRY(backend.evolve(target - backend.timeS()));
        const double spent = std::chrono::duration<double>(Clock::now() - before).count();
        if (spent < 0.02) chunk *= 2.0;
        const auto now = Clock::now();
        if (std::chrono::duration<double>(now - lastPublish).count() >= 0.1) { // 10 Hz (spec 11 §5)
            lastPublish = now;
            LinePowerEvent ev;
            ev.run = in.id;
            ev.device = in.device ? in.device->id : std::string{};
            ev.wallTimeS = std::chrono::duration<double>(now - started).count();
            ev.progress = pm.durationS > 0.0 ? backend.timeS() / pm.durationS : 1.0;
            ev.powers = powers;
            if (in.events) in.events->post(std::move(ev));
        }
    }
    publish(1.0);

    out.lindblad.assign(backend.trajectory().begin(), backend.trajectory().end());
    const num::Matrix reduced = computationalRho(backend.rho(), pm.siteDims, pm.qubitSites);
    {
        qsim::Snapshot snap;
        snap.kind = qsim::Kind::Lindblad;
        snap.nQubits = pm.qubitSites;
        snap.levels = *std::max_element(pm.siteDims.begin(), pm.siteDims.end());
        snap.simTimePs = backend.timeS() * 1e12;
        snap.cls = data::FidelityClass::Numerical;
        snap.densityMatrix = reduced;
        std::vector<double> probs(reduced.rows, 0.0);
        for (std::size_t i = 0; i < reduced.rows; ++i) probs[i] = reduced(i, i).real();
        snap.probabilities = probs;
        out.finalState = std::make_shared<const qsim::Snapshot>(std::move(snap));
        if (options.cadence != SnapshotCadence::None) {
            RunSnapshot rs;
            rs.timeS = backend.timeS();
            rs.gateIndex = in.program->circuit.topologicalOrder().size();
            rs.state = out.finalState;
            rs.measuredBits.assign(plan.nQubits(), -1);
            rs.probes = evaluateProbes(reduced, pm.qubitSites, plan, options.probes);
            out.snapshots.push_back(std::move(rs));
        }
    }

    // Spec 15 §3.5 (c): terminal measurement is the calibrated readout POVM on the final ρ.
    out.memory.assign(in.shots, ShotRecord{std::vector<std::uint8_t>(plan.layout.bits, 0), {}, false});
    if (!plan.measuredQubits.empty()) {
        QXL_TRY_ASSIGN(noise::ReadoutModel readout, detail::terminalReadoutFor(in, plan));
        std::vector<double> ideal(std::size_t{1} << plan.measuredQubits.size(), 0.0);
        for (std::size_t i = 0; i < reduced.rows; ++i) {
            std::size_t index = 0;
            for (std::size_t k = 0; k < plan.measuredQubits.size(); ++k)
                index |= ((i >> plan.measuredQubits[k]) & 1) << k;
            ideal[index] += reduced(i, i).real();
        }
        QXL_TRY_ASSIGN(const std::vector<double> observed, readout.applyToProbabilities(ideal));
        const std::vector<std::int64_t> cbits = detail::classicalBitOfMeasured(plan);
        // The exact Born distribution over the classical bits (Simulator-only, spec 15 §4).
        if (plan.layout.bits > 0 && plan.layout.bits <= 20) {
            std::vector<double> dist(std::size_t{1} << plan.layout.bits, 0.0);
            for (std::size_t i = 0; i < observed.size(); ++i) {
                std::size_t index = 0;
                for (std::size_t k = 0; k < cbits.size(); ++k)
                    if (cbits[k] >= 0 && ((i >> k) & 1)) index |= std::size_t{1} << static_cast<std::size_t>(cbits[k]);
                dist[index] += observed[i];
            }
            out.exact = std::move(dist);
        }
        core::Random rng(in.seed);
        for (std::uint32_t s = 0; s < in.shots; ++s) {
            double r = rng.uniform(), acc = 0.0;
            std::size_t pick = observed.empty() ? 0 : observed.size() - 1;
            for (std::size_t i = 0; i < observed.size(); ++i) {
                acc += observed[i];
                if (r < acc) { pick = i; break; }
            }
            for (std::size_t k = 0; k < cbits.size(); ++k)
                if (cbits[k] >= 0 && static_cast<std::size_t>(cbits[k]) < out.memory[s].bits.size())
                    out.memory[s].bits[static_cast<std::size_t>(cbits[k])] = static_cast<std::uint8_t>((pick >> k) & 1);
        }
    }
    out.shotsCompleted = out.partial ? 0 : in.shots;
    out.exactClass = data::FidelityClass::Numerical;
    out.cls = data::FidelityClass::Numerical; // spec 07 §5: the Lindblad backend is Numerical
    if (out.partial) out.diagnostics.push_back(diagnostic("QL5060", SourceSpan{}));
    if (in.progress) in.progress(out.shotsCompleted, in.shots);
    return out;
}

} // namespace qlab::runtime
