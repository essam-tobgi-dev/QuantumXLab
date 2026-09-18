// Spec 08 §7.4, T04 §3–4 / spec 25 §3.5 — the collapse operators of the noise model, integrated by
// qsim::LindbladBackend, reproduce T1 and T2 decay, the thermal fixed point and the Kraus channel.
#include "NoiseTestSupport.hpp"
#include <catch2/catch_approx.hpp>

using namespace ntest;
using namespace qlab::noise;
using Catch::Approx;

namespace {
NoiseModel twoQubitModel() {
    auto m = NoiseModel::fromJson(core::Json::parse(R"({"qubits": {
        "0": {"t1_us": 50, "t2_us": 60, "p_thermal": 0.02},
        "1": {"t1_us": 80, "t2_us": 30}}})"));
    NOISE_REQUIRE_OK(m);
    return std::move(*m);
}
qsim::LindbladBackend backendFor(std::vector<std::uint32_t> dims, std::vector<qsim::CollapseOp> ops) {
    qsim::SystemModel sys;
    sys.siteDims = dims;
    sys.h0 = Matrix(sys.dimension(), sys.dimension());
    sys.frameFrequenciesHz.assign(dims.size(), 0.0);
    sys.collapse = std::move(ops);
    qsim::LindbladBackend lb;
    qsim::LindbladSettings settings;
    settings.stepS = 0.2e-6; // rates ≤ 3e4 /s: RK4 local error ~ (h/τ)^5 is negligible
    settings.sampleS = 5e-6;
    settings.recordTrajectory = false;
    lb.setSettings(settings);
    NOISE_REQUIRE_OK(lb.setModel(std::move(sys)));
    return lb;
}
} // namespace

TEST_CASE("Lindblad collapse operators reproduce T1 decay via qsim::LindbladBackend") {
    const NoiseModel m = twoQubitModel();
    auto ops = m.lindbladOperators(q({0}), std::vector<std::uint32_t>{2});
    NOISE_REQUIRE_OK(ops);
    REQUIRE(ops->size() == 3); // √((1−p_th)/T1)σ⁻, √(p_th/T1)σ⁺, √(γφ/2)Z — no gate depolarizing
    REQUIRE((*ops)[0].name == "T1 q0");
    REQUIRE((*ops)[1].name == "Tth q0");
    REQUIRE((*ops)[2].name == "Tphi q0");
    const double t1 = 50e-6, t2 = 60e-6, pth = 0.02, gphi = 1.0 / t2 - 0.5 / t1;
    REQUIRE(std::abs((*ops)[0].op(0, 1)) == Approx(std::sqrt((1.0 - pth) / t1)).epsilon(1e-15));
    REQUIRE(std::abs((*ops)[1].op(1, 0)) == Approx(std::sqrt(pth / t1)).epsilon(1e-15));
    REQUIRE((*ops)[2].op(0, 0).real() == Approx(std::sqrt(gphi / 2)).epsilon(1e-15));

    auto excited = backendFor({2}, *ops);
    NOISE_REQUIRE_OK(excited.setPure(std::vector<Complex>{0, 1}));
    auto plus = backendFor({2}, *ops);
    NOISE_REQUIRE_OK(plus.setPure(std::vector<Complex>{1.0 / std::numbers::sqrt2, 1.0 / std::numbers::sqrt2}));
    for (int step = 1; step <= 12; ++step) {
        NOISE_REQUIRE_OK(excited.evolve(5e-6));
        NOISE_REQUIRE_OK(plus.evolve(5e-6));
        const double t = 5e-6 * step;
        INFO("t = " << t);
        REQUIRE(excited.population(0, 1) == Approx(pth + (1.0 - pth) * std::exp(-t / t1)).margin(1e-6)); // spec 25 §3.5
        REQUIRE(std::abs(plus.rho()(0, 1)) == Approx(0.5 * std::exp(-t / t2)).margin(1e-6));
    }
    // Same state as the Kraus form of thermal_relaxation over the whole 60 µs (spec 08 §2.3 vs §7.4).
    core::Random rng(1);
    qsim::DensityMatrixBackend dm;
    NOISE_REQUIRE_OK(dm.allocate(1));
    NOISE_REQUIRE_OK(dm.applyGate(H(), q({0})));
    ApplyReport report;
    NOISE_REQUIRE_OK(applyChannels(dm, m.idleChannels(QubitIndex{0}, 60e-6), rng, {}, report));
    REQUIRE(num::approxEqual(dm.rho(), plus.rho(), 1e-6));
    // Long times: the thermal fixed point p_th.
    NOISE_REQUIRE_OK(excited.evolve(1e-3));
    REQUIRE(excited.population(0, 1) == Approx(pth).margin(1e-6));
}

TEST_CASE("collapse operators embed per site, including three-level transmon sites") {
    const NoiseModel m = twoQubitModel();
    const std::vector<std::uint32_t> dims{2, 3};
    auto ops = m.lindbladOperators(q({0, 1}), dims);
    NOISE_REQUIRE_OK(ops);
    REQUIRE(ops->size() == 5); // qubit 1 has no thermal population: no up-rate term
    for (const auto& op : *ops) { REQUIRE(op.op.rows == 6); REQUIRE(op.op.cols == 6); }
    auto lb = backendFor(dims, *ops);
    std::vector<Complex> psi(6, 0.0);
    psi[0 + 2 * 1] = 1.0; // site 0 in |0⟩, site 1 in |1⟩ (index = i0 + 2 i1)
    NOISE_REQUIRE_OK(lb.setPure(psi));
    NOISE_REQUIRE_OK(lb.evolve(40e-6));
    REQUIRE(lb.population(1, 1) == Approx(std::exp(-40e-6 / 80e-6)).margin(1e-6)); // √(1/T1)·a on the qutrit
    REQUIRE(lb.population(1, 2) == Approx(0.0).margin(1e-12));                      // no up-rate, no leakage
    REQUIRE(lb.population(0, 1) == Approx(0.02 * (1.0 - std::exp(-40e-6 / 50e-6))).margin(1e-6));
    // Coherence of the qutrit's qubit subspace still decays with its T2 (a†a dephasing, spec 08 (7.2)).
    // The sites evolve independently, so ρ(|00⟩,|10⟩) = ⟨0|ρ_site0|0⟩ · ρ_site1(0, 1).
    std::vector<Complex> sup(6, 0.0);
    sup[0] = sup[2] = 1.0 / std::numbers::sqrt2;
    NOISE_REQUIRE_OK(lb.setPure(sup));
    const double t0 = lb.timeS();
    NOISE_REQUIRE_OK(lb.evolve(20e-6));
    REQUIRE(lb.timeS() - t0 == Approx(20e-6).epsilon(1e-9));
    const double site0Ground = 1.0 - 0.02 * (1.0 - std::exp(-20e-6 / 50e-6));
    REQUIRE(std::abs(lb.rho()(0, 2)) == Approx(site0Ground * 0.5 * std::exp(-20e-6 / 30e-6)).margin(1e-6));

    // Misuse is reported, and disabling thermal relaxation removes the operators.
    REQUIRE_FALSE(m.lindbladOperators(q({0, 1}), std::vector<std::uint32_t>{2}));
    REQUIRE_FALSE(m.lindbladOperators(q({7}), std::vector<std::uint32_t>{2}));
    NoiseModel quiet = m;
    Overrides o;
    o.setEnabled(id::ThermalRelaxation, false);
    NOISE_REQUIRE_OK(quiet.setOverrides(o));
    auto none = quiet.lindbladOperators(std::vector<std::uint32_t>{2, 2});
    NOISE_REQUIRE_OK(none);
    REQUIRE(none->empty());
}
