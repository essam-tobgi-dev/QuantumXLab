// Spec 15 §3–§4, §7 (ii) — a calibration-derived noisy run: the density-matrix backend lowers the
// Bell success probability by the amount the calibration predicts, and the accurate fidelity of
// §7 (ii) compares it with the ideal run.
#include "RuntimeTestUtil.hpp"
#include "Data/Fidelity.hpp"
#include <cmath>

using namespace rtest;
using Catch::Approx;

namespace {
RunOptions noisyBell(std::uint64_t seed) {
    RunOptions o;
    o.noise = NoiseSource::Calibrated;
    o.shots = 8192;
    o.seed = seed;
    o.accurateFidelity = true;
    return o;
}
} // namespace

TEST_CASE("a noisy density-matrix run lowers the Bell fidelity as the calibration predicts") {
    Lab& l = lab("sc_fixed_5");
    const RunResult r = l.run(readAsset("Programs/Examples/Basics/bell.qasm"), noisyBell(11));
    REQUIRE(r.backend == qsim::Kind::DensityMatrix);
    REQUIRE(r.backendClass == data::FidelityClass::Numerical);
    REQUIRE(r.exact.has_value());
    REQUIRE(r.exactClass == data::FidelityClass::Exact);

    // The stored distribution is the OBSERVED one (readout POVM applied), so the sampled counts
    // agree with it shot for shot within the binomial error.
    REQUIRE(r.counts.theory.has_value());
    const auto all = r.counts.all();
    REQUIRE(all.size() == 4);
    for (std::size_t i = 0; i < all.size(); ++i) {
        const double measured = static_cast<double>(all[i].second) / 8192.0;
        const double theory = (*r.counts.theory)[i];
        INFO(all[i].first << ": measured " << measured << ", exact " << theory);
        REQUIRE(std::abs(measured - theory) < 5.0 * sigma(theory, 8192));
    }

    // Errors are visible: the 01 and 10 outcomes are populated, and the success probability sits
    // below 1 by roughly what the product model of §7 (i) predicts.
    const double success = r.probability("00") + r.probability("11");
    const double leak = r.probability("01") + r.probability("10");
    REQUIRE(success < 1.0);
    REQUIRE(leak > 0.0);
    REQUIRE(success + leak == Approx(1.0).margin(1e-12));
    const double fast = r.estimate.fidelity.fast;
    INFO("success " << success << " vs fast estimate " << fast);
    REQUIRE(fast < 1.0);
    REQUIRE(std::abs(success - fast) < 0.05);            // the two models agree to a few percent
    REQUIRE(success > 0.5 * fast);                        // spec 15 §7: a factor 2 would be a red flag
    // The dominant term is readout: two qubits at the calibrated assignment fidelity.
    double roProduct = 1.0;
    for (std::uint32_t q : r.qubits) roProduct *= l.session.calibration()->qubit(q)->readoutFidelity();
    REQUIRE(r.estimate.fidelity.readoutProduct == Approx(roProduct).epsilon(1e-12));

    // §7 (ii): the noisy-versus-ideal comparison.
    REQUIRE(r.estimate.fidelity.simulated.has_value());
    const SimulatedFidelity& s = *r.estimate.fidelity.simulated;
    REQUIRE(s.cls == data::FidelityClass::Numerical);
    REQUIRE(s.classical > 0.8);
    REQUIRE(s.classical < 1.0);
    REQUIRE(s.hellinger == Approx(std::sqrt(1.0 - std::sqrt(s.classical))).epsilon(1e-12));
    REQUIRE(s.state.has_value());
    REQUIRE(*s.state > 0.9);
    REQUIRE(*s.state < 1.0);
    // F_c of two distributions that differ only by the readout map: the classical fidelity is the
    // sum over the shared outcomes of √(p_ideal p_noisy), squared.
    const std::vector<double>& p = *r.exact;
    const double expected = std::pow(std::sqrt(0.5 * p[0]) + std::sqrt(0.5 * p[3]), 2.0);
    REQUIRE(s.classical == Approx(expected).margin(1e-9));
}

TEST_CASE("an ideal run of the same circuit is exact and has no simulated comparison") {
    Lab& l = lab("sc_fixed_5");
    RunOptions o = noisyBell(11);
    o.noise = NoiseSource::Ideal;
    const RunResult r = l.run(readAsset("Programs/Examples/Basics/bell.qasm"), o);
    REQUIRE(r.backend == qsim::Kind::StateVector);
    REQUIRE(r.backendClass == data::FidelityClass::Exact);
    REQUIRE(r.probability("00") + r.probability("11") == Approx(1.0).margin(1e-12));
    REQUIRE_FALSE(r.estimate.fidelity.simulated.has_value()); // nothing to compare an ideal run with
    // The estimate still describes the hardware, which is noisy.
    REQUIRE(r.estimate.fidelity.fast < 1.0);
    REQUIRE(r.estimate.cls == data::FidelityClass::Model);
}

TEST_CASE("noise trajectories on the state vector agree with the density matrix within statistics") {
    Lab& l = lab("sc_fixed_5");
    RunOptions dm = noisyBell(21);
    dm.accurateFidelity = false;
    dm.backend = BackendChoice::DensityMatrix;
    const RunResult exact = l.run(readAsset("Programs/Examples/Basics/bell.qasm"), dm);
    REQUIRE(exact.backend == qsim::Kind::DensityMatrix);

    RunOptions traj = dm;
    traj.backend = BackendChoice::StateVector;
    const RunResult sampled = l.run(readAsset("Programs/Examples/Basics/bell.qasm"), traj);
    REQUIRE(sampled.backend == qsim::Kind::StateVector);
    REQUIRE(sampled.backendClass == data::FidelityClass::Statistical);
    REQUIRE(sampled.memory.size() == 8192);
    for (const char* key : {"00", "01", "10", "11"}) {
        const double a = exact.probability(key), b = sampled.probability(key);
        INFO(key << ": density matrix " << a << ", trajectories " << b);
        REQUIRE(std::abs(a - b) < 5.0 * std::sqrt(sigma(a, 8192) * sigma(a, 8192) + sigma(b, 8192) * sigma(b, 8192)));
    }
}

TEST_CASE("the noise model is built from the session's calibration and reacts to it") {
    Lab& l = lab("sc_fixed_5");
    const RunResult base = l.run(readAsset("Programs/Examples/Basics/bell.qasm"), noisyBell(31));

    // A calibration with ten times the gate error and half the coherence must lower both the fast
    // estimate and the measured success probability.
    hw::LoadedDevice worse{*l.session.device(), *l.session.calibration(), {}};
    for (hw::QubitCal& q : worse.calibration.qubits) {
        q.gateError1q.value *= 10.0;
        q.t1.value = units::Time{q.t1.value.v * 0.1};
        q.t2echo.value = units::Time{q.t2echo.value.v * 0.1};
        q.t2star.value = units::Time{q.t2star.value.v * 0.1};
        q.readoutAssignment = {{{0.90, 0.10}, {0.10, 0.90}}};
    }
    for (auto& [key, e] : worse.calibration.edges) e.gateError2q.value = std::min(0.2, e.gateError2q.value * 10.0);

    Lab degraded("sc_fixed_5");
    degraded.session.setDevice(std::move(worse));
    const RunResult bad = degraded.run(readAsset("Programs/Examples/Basics/bell.qasm"), noisyBell(31));
    INFO("base " << base.probability("00") + base.probability("11") << " vs degraded "
                 << bad.probability("00") + bad.probability("11"));
    REQUIRE(bad.estimate.fidelity.fast < base.estimate.fidelity.fast);
    REQUIRE(bad.probability("00") + bad.probability("11") < base.probability("00") + base.probability("11"));
    REQUIRE(bad.estimate.fidelity.readoutProduct == Approx(0.9 * 0.9).epsilon(1e-12));
}
