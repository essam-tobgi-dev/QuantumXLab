// Spec 15 §3.5 (c), spec 11 §5, T05 (7.1), T06 (6.1) — pulse-level runs: the Lindblad evolution of
// the compiled schedule on the device Hamiltonian, the virtual-Z frame convention, the Mølmer–
// Sørensen gate on the ion chain, and the per-line drive power published to cryo.
#include "RuntimeTestUtil.hpp"
#include "Data/Fidelity.hpp"
#include <cmath>

using namespace rtest;
using Catch::Approx;

namespace {
using num::Complex;

RunOptions pulseRun(std::uint32_t fock = 8, double stepS = 0.0) {
    RunOptions o;
    o.noise = NoiseSource::Ideal;      // decoherence off: the oracle is the coherent evolution
    o.pulseDecoherence = false;
    o.shots = 16;
    o.seed = 3;
    o.fockCutoff = fock;
    o.pulseStepS = stepS;
    return o;
}

compiler::CompileOptions pulseCompile() {
    compiler::CompileOptions c;
    c.pulseLevel = true;
    return c;
}

// F = ⟨ψ|ρ|ψ⟩ (squared convention, T10 §1).
double fidelity(const num::Matrix& rho, const std::vector<Complex>& psi) {
    REQUIRE(rho.rows == psi.size());
    Complex f = 0.0;
    for (std::size_t i = 0; i < psi.size(); ++i)
        for (std::size_t j = 0; j < psi.size(); ++j) f += std::conj(psi[i]) * rho(i, j) * psi[j];
    REQUIRE(std::abs(f.imag()) < 1e-9);
    return f.real();
}

// R_z(θ) on one qubit of an n-qubit amplitude vector: |k⟩ picks up e^{−iθ/2} or e^{+iθ/2}.
std::vector<Complex> rotateZ(std::vector<Complex> psi, std::uint32_t qubit, double theta) {
    for (std::size_t k = 0; k < psi.size(); ++k)
        psi[k] *= std::polar(1.0, ((k >> qubit) & 1) ? 0.5 * theta : -0.5 * theta);
    return psi;
}
} // namespace

TEST_CASE("pulse level: sx on sc_fixed_5 prepares R_x(pi/2)|0> to better than 1e-3") {
    Lab& l = lab("sc_fixed_5");
    const RunResult r = l.run(source("pragma qlab.layout physical\nsx $0;\n"), pulseRun(), pulseCompile());

    REQUIRE(r.backend == qsim::Kind::Lindblad);
    REQUIRE(r.backendClass == data::FidelityClass::Numerical);
    REQUIRE(r.levels == 3);                    // the transmon leakage level (spec 07 §5)
    REQUIRE(r.schedule != nullptr);
    REQUIRE(r.schedule->duration().get() > 0);
    REQUIRE(r.finalState != nullptr);
    REQUIRE(r.finalState->densityMatrix.has_value());
    REQUIRE(!r.lindblad.empty());
    for (const qsim::TimeSample& s : r.lindblad) REQUIRE(std::abs(s.trace - 1.0) < 1e-9);
    REQUIRE(r.lindblad.back().timeS == Approx(r.schedule->duration().get() * 1e-12).margin(1e-15));
    REQUIRE(r.lindblad.back().leakage < 1e-3); // a calibrated DRAG sx barely leaves the qubit subspace

    // R_x(π/2)|0⟩ = (|0⟩ − i|1⟩)/√2; the + i sign would mean the drive quadrature is mirrored.
    const double s2 = 1.0 / std::sqrt(2.0);
    const std::vector<Complex> target{{s2, 0.0}, {0.0, -s2}};
    const num::Matrix& rho = *r.finalState->densityMatrix;
    REQUIRE(rho.rows == 2);
    INFO("F = " << fidelity(rho, target));
    REQUIRE(fidelity(rho, target) > 0.999);
    REQUIRE(fidelity(rho, {{s2, 0.0}, {0.0, s2}}) < 0.01); // the mirrored quadrature is excluded
    REQUIRE(rho(0, 0).real() == Approx(0.5).margin(2e-3));
    REQUIRE(rho(1, 1).real() == Approx(0.5).margin(2e-3));
}

TEST_CASE("pulse level: rz then sx matches the gate-level state up to the frame the virtual Z leaves") {
    Lab& l = lab("sc_fixed_5");
    const std::string text = source("pragma qlab.layout physical\nrz(1.5707963267948966) $0;\nsx $0;\n");
    const RunResult pulse = l.run(text, pulseRun(), pulseCompile());
    REQUIRE(pulse.backend == qsim::Kind::Lindblad);

    // The same circuit on the gate-level state vector.
    RunOptions gateLevel;
    gateLevel.noise = NoiseSource::Ideal;
    gateLevel.shots = 1;
    gateLevel.seed = 3;
    const RunResult gate = l.run(text, gateLevel);
    REQUIRE(gate.backend == qsim::Kind::StateVector);
    REQUIRE(gate.finalState->amplitudes.has_value());
    const std::vector<Complex> psi(gate.finalState->amplitudes->begin(), gate.finalState->amplitudes->end());
    REQUIRE(psi.size() == 2);

    // T07 §3: a virtual Z is a frame shift, so the pulse-level state is the gate-level state with the
    // accumulated frame rotation R_z(-phi) still to be applied. Populations are frame-independent.
    const num::Matrix& rho = *pulse.finalState->densityMatrix;
    for (std::size_t k = 0; k < 2; ++k) REQUIRE(rho(k, k).real() == Approx(std::norm(psi[k])).margin(2e-3));
    const std::vector<Complex> framed = rotateZ(psi, 0, -kPi / 2.0);
    INFO("F = " << fidelity(rho, framed));
    REQUIRE(fidelity(rho, framed) > 0.999);
    // The opposite frame direction is excluded: this is what a mirrored virtual Z or a mirrored
    // DRAG quadrature would produce.
    REQUIRE(fidelity(rho, rotateZ(psi, 0, kPi / 2.0)) < 0.01);
    // Concretely: the pulse-level state is (|0⟩ − |1⟩)/√2, the gate-level one (|0⟩ − i|1⟩)/√2.
    const double s2 = 1.0 / std::sqrt(2.0);
    REQUIRE(fidelity(rho, {{s2, 0.0}, {-s2, 0.0}}) > 0.999);
}

TEST_CASE("pulse level: the Molmer-Sorensen gate on ion_chain_11 makes a Bell state and closes the loop") {
    Lab& l = lab("ion_chain_11");
    const std::string text = source("pragma qlab.layout physical\nms(1.5707963267948966) $0, $1;\n");
    // 20 ps steps would resolve the 0.3 MHz motional mode 10^5 times over; 20 ns already converges
    // the answer (the 2 ns result agrees to 2e-5), and keeps a 200 µs gate affordable.
    const RunResult r = l.run(text, pulseRun(8, 20e-9), pulseCompile());
    REQUIRE(r.backend == qsim::Kind::Lindblad);
    REQUIRE(r.levels == 2);                       // ion qubits are two-level; the mode is the extra site
    REQUIRE(r.finalState->densityMatrix.has_value());
    const num::Matrix& rho = *r.finalState->densityMatrix;
    REQUIRE(rho.rows == 4);

    // T06 (6.1) with the shipped sign convention: XX(π/2) on |00⟩ gives (|00⟩ − i|11⟩)/√2.
    const double s2 = 1.0 / std::sqrt(2.0);
    const std::vector<Complex> bell{{s2, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {0.0, -s2}};
    INFO("F = " << fidelity(rho, bell));
    REQUIRE(fidelity(rho, bell) > 0.99);
    REQUIRE(fidelity(rho, {{s2, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {0.0, s2}}) < 0.05); // the mirrored phase
    REQUIRE(rho(0, 0).real() == Approx(0.5).margin(0.02));
    REQUIRE(rho(3, 3).real() == Approx(0.5).margin(0.02));
    REQUIRE(rho(1, 1).real() < 0.01);             // |01⟩ and |10⟩ stay empty
    REQUIRE(rho(2, 2).real() < 0.01);

    // Loop closure: the motional mode returns to |n = 0⟩. What is left is the Fock truncation, so
    // a smaller cutoff is measurably worse (checked below). The bound is the total dimension
    // (D ≤ 243), which for two ions allows a cutoff far above the default 8.
    REQUIRE(r.lindblad.size() > 1);
    const qsim::TimeSample& last = r.lindblad.back();
    REQUIRE(last.populations.size() == 2 + 2 + 8);
    const double mode0 = last.populations[4]; // P(n = 0) of the motional mode
    INFO("P(n = 0) = " << mode0);
    REQUIRE(1.0 - mode0 < 3e-3);
    REQUIRE(std::abs(last.trace - 1.0) < 1e-9);

    // The answer is converged in the integrator step.
    const RunResult coarse = l.run(text, pulseRun(8, 40e-9), pulseCompile());
    REQUIRE(fidelity(*coarse.finalState->densityMatrix, bell) == Approx(fidelity(rho, bell)).margin(1e-3));

    // A smaller Fock space is measurably worse, which is what makes the cap the limiting factor.
    const RunResult small = l.run(text, pulseRun(4, 40e-9), pulseCompile());
    REQUIRE(fidelity(*small.finalState->densityMatrix, bell) < fidelity(rho, bell));
    REQUIRE(1.0 - small.lindblad.back().populations[4] > 1.0 - mode0);
    // The cutoff of 5 that the per-site cap used to force is also worse than the default 8, which
    // is why the cap mattered: it bounded the accuracy of every ion gate.
    const RunResult atFive = l.run(text, pulseRun(5, 40e-9), pulseCompile());
    INFO("residue at cutoff 5 = " << 1.0 - atFive.lindblad.back().populations[4]
                                  << ", at 8 = " << 1.0 - mode0);
    REQUIRE(1.0 - atFive.lindblad.back().populations[4] > 1.0 - mode0);
    REQUIRE(fidelity(*atFive.finalState->densityMatrix, bell) < fidelity(rho, bell));
}

TEST_CASE("pulse level: the Fock cutoff is bounded by the total dimension, not a per-site cap") {
    // The Lindblad cap is D <= 243, so two ions admit a motional cutoff up to 60 (2*2*60 = 240).
    // A cutoff of 8 is the default because truncating at 5 leaves a measurable residue in the mode.
    Lab& l = lab("ion_chain_11");
    const Lab::Job job = l.compile(source("pragma qlab.layout physical\nms(1.5707963267948966) $0, $1;\n"),
                                   pulseCompile());
    auto ok = l.session.runSync(l.request(job, pulseRun(8, 40e-9)));
    if (!ok) UNSCOPED_INFO(ok.error().format());
    REQUIRE(ok.has_value());

    // Above the total-dimension cap the request is refused, never clamped behind the user's back.
    auto out = l.session.runSync(l.request(job, pulseRun(64, 40e-9)));
    REQUIRE_FALSE(out.has_value());
    INFO(out.error().format());
    REQUIRE((out.error().code == err::LindbladCap || out.error().code == qsim::err::TooLarge));

    // A site needs at least two levels.
    auto tiny = l.session.runSync(l.request(job, pulseRun(1, 40e-9)));
    REQUIRE_FALSE(tiny.has_value());
}

TEST_CASE("spec 11 section 5: the schedule's average line power is published to cryo at 10 Hz") {
    Lab& l = lab("sc_fixed_5");
    l.bus.drain(); // discard what earlier cases queued on the shared session's bus
    std::vector<LinePowerEvent> events;
    auto sub = l.bus.subscribe<LinePowerEvent>([&](const LinePowerEvent& e) { events.push_back(e); });
    const RunResult r = l.run(source("pragma qlab.layout physical\nsx $0;\nsx $1;\n"), pulseRun(), pulseCompile());
    REQUIRE(r.schedule != nullptr);
    l.bus.drain();

    REQUIRE(events.size() >= 2);
    REQUIRE(events.front().progress == Approx(0.0));
    REQUIRE(events.back().progress == Approx(1.0));
    REQUIRE(events.back().powers.empty());     // the lines are quiet once the job ends
    REQUIRE(events.front().device == "sc_fixed_5");
    const cryo::LinePowers& p = events.front().powers;
    REQUIRE(p.size() >= 2);
    // Keys are the fridge line ids of the device's wiring.json, not the channel names (spec 11 §9).
    REQUIRE(p.contains("drive_q0"));
    REQUIRE(p.contains("drive_q1"));
    for (const auto& [line, watts] : p) {
        INFO(line << " = " << watts << " W");
        REQUIRE(watts > 0.0);
        REQUIRE(watts < 1e-3);                 // a 32 ns pulse averaged over the repetition period
    }
    // The same figure computed directly: P = (1/T_rep) Σ ∫ (A V_fs |e|)²/(2 Z₀) dt.
    auto direct = schedulePower(*r.schedule, *l.session.device(), l.session.device()->timing.repetitionDelay.v);
    REQUIRE(direct);
    REQUIRE(direct->size() == p.size());
    for (const auto& [line, watts] : *direct) REQUIRE(p.at(line) == Approx(watts).epsilon(1e-12));
    // Twice the repetition period is half the average power: the energy per shot is what is fixed.
    auto halved = schedulePower(*r.schedule, *l.session.device(), 2.0 * l.session.device()->timing.repetitionDelay.v);
    REQUIRE(halved);
    for (const auto& [line, watts] : *halved) REQUIRE(watts == Approx(0.5 * direct->at(line)).epsilon(1e-12));
}
