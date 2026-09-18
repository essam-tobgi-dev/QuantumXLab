// Spec 09 §5.5 — the time-domain model handed to the Lindblad backend (spec 07 §5).
#include "Hardware/Hardware.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qlab;
using namespace qlab::hw;
using Catch::Approx;

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
SystemModelOptions opts(std::span<const std::uint32_t> qs) {
    SystemModelOptions o;
    o.qubits = qs;
    return o;
}
} // namespace

TEST_CASE("site operators embed little-endian") {
    const std::vector<std::uint32_t> dims{3, 3};
    auto a0 = siteAnnihilate(dims, 0);
    auto a1 = siteAnnihilate(dims, 1);
    REQUIRE(a0.rows == 9);
    // Site 0 is the least significant index: |n1 n0> has index n0 + 3 n1.
    REQUIRE(a0.at(0, 1).real() == Approx(1.0));  // |01> -> |00>
    REQUIRE(a1.at(0, 3).real() == Approx(1.0));  // |10> -> |00>
    REQUIRE(a0.at(0, 3) == num::Complex(0.0, 0.0));
    auto n0 = siteNumber(dims, 0);
    REQUIRE(n0.at(1, 1).real() == Approx(1.0));
    REQUIRE(n0.at(2, 2).real() == Approx(2.0));
    REQUIRE(n0.at(3, 3).real() == Approx(0.0));
    auto p2 = siteProjector(dims, 0, 2);
    REQUIRE(p2.at(2, 2).real() == Approx(1.0));
    REQUIRE(p2.at(1, 1).real() == Approx(0.0));
}

TEST_CASE("a transmon model is Hermitian with the right dimension and frame") {
    auto ld = loadShippedDevice("sc_fixed_5");
    REQUIRE(ld);
    const std::vector<std::uint32_t> qs{0, 1};
    auto m = buildSystemModel(ld->device, ld->calibration, opts(qs));
    if (!m) FAIL(m.error().format());
    REQUIRE(m->siteDims == std::vector<std::uint32_t>{3, 3});
    REQUIRE(m->dimension() == 9);
    REQUIRE(m->h0.rows == 9);
    REQUIRE(num::isHermitian(m->h0Dense()));
    REQUIRE(m->frame == "per_qubit_rotating");
    REQUIRE(m->qubitIndices == qs);
    // Each site rotates at its own f01, so the diagonal detuning vanishes and only the
    // anharmonic ladder plus exchange survive.
    REQUIRE(m->frameFrequenciesHz[0] == Approx(ld->calibration.qubits[0].f01.value.v));
    REQUIRE(m->h0.at(0, 0).real() == Approx(0.0).margin(1e-6));
    const double alpha0 = ld->calibration.qubits[0].anharmonicity.value.v;
    REQUIRE(m->h0.at(2, 2).real() == Approx(kTwoPi * alpha0).epsilon(1e-9)); // |02>: (alpha/2)*2*1
}

TEST_CASE("a shared frame leaves the detuning in H0") {
    auto ld = loadShippedDevice("sc_fixed_5");
    REQUIRE(ld);
    const std::vector<std::uint32_t> qs{0, 1};
    auto o = opts(qs);
    o.frameFrequencyHz = 5.0e9;
    auto m = buildSystemModel(ld->device, ld->calibration, o);
    REQUIRE(m);
    const double f0 = ld->calibration.qubits[0].f01.value.v;
    REQUIRE(m->h0.at(1, 1).real() == Approx(kTwoPi * (f0 - 5.0e9)).epsilon(1e-9));
    REQUIRE(num::isHermitian(m->h0Dense()));
}

TEST_CASE("drives cover every qubit and native cross-resonance direction") {
    auto ld = loadShippedDevice("sc_fixed_5");
    REQUIRE(ld);
    const std::vector<std::uint32_t> qs{0, 1};
    auto m = buildSystemModel(ld->device, ld->calibration, opts(qs));
    REQUIRE(m);
    bool d0 = false, d1 = false, cr = false;
    for (const auto& d : m->drives) {
        if (d.channel == "d[0]") d0 = true;
        if (d.channel == "d[1]") d1 = true;
        if (d.channel == "u[0,1]") {
            cr = true;
            // A CR channel drives the control but rotates at the target frequency (T05 §8).
            REQUIRE(d.site == 0);
            REQUIRE(d.target.has_value());
            REQUIRE(*d.target == 1u);
            REQUIRE(d.frameFrequencyHz == Approx(ld->calibration.qubits[1].f01.value.v));
        }
        REQUIRE(d.inPhase.rows == m->dimension());
        REQUIRE(num::isHermitian(d.inPhase.toDense()));
        REQUIRE(num::isHermitian(d.quadrature.toDense()));
    }
    REQUIRE(d0);
    REQUIRE(d1);
    REQUIRE(cr);
}

TEST_CASE("drive operators reduce to X/2 and +Y/2 on the qubit levels (T05 (7.1))") {
    // Regression: the quadrature was built as i(a − a†)/2 = −Y/2, which mirrors the DRAG
    // quadrature and the virtual-Z direction in every pulse-level run.
    auto ld = loadShippedDevice("sc_fixed_5");
    REQUIRE(ld);
    const std::vector<std::uint32_t> qs{0};
    auto m = buildSystemModel(ld->device, ld->calibration, opts(qs));
    REQUIRE(m);
    const DriveSpec* drive = nullptr;
    for (const auto& d : m->drives)
        if (d.channel == "d[0]") drive = &d;
    REQUIRE(drive != nullptr);
    const auto X = drive->inPhase.toDense();
    const auto Y = drive->quadrature.toDense();
    // Little-endian single site: level 0 is |0>, level 1 is |1>.
    REQUIRE(std::abs(X(0, 1) - num::Complex(0.5, 0.0)) < 1e-14);
    REQUIRE(std::abs(X(1, 0) - num::Complex(0.5, 0.0)) < 1e-14);
    REQUIRE(std::abs(Y(0, 1) - num::Complex(0.0, -0.5)) < 1e-14); // Y = [[0, -i], [i, 0]]
    REQUIRE(std::abs(Y(1, 0) - num::Complex(0.0, 0.5)) < 1e-14);
    // [(a+a†)/2, i(a†−a)/2] = (i/2)[a, a†], and for a ladder truncated at d levels
    // [a, a†] = diag(1, 1, …, 1, −(d−1)). The sign of the first element fixes the handedness.
    const auto c = num::sub(num::matmul(X, Y), num::matmul(Y, X));
    const std::size_t d = m->siteDims[0];
    REQUIRE(d >= 2);
    for (std::size_t k = 0; k < d; ++k) {
        const double expected = (k + 1 < d) ? 0.5 : -0.5 * static_cast<double>(d - 1);
        INFO("level " << k << " of " << d);
        REQUIRE(std::abs(c(k, k) - num::Complex(0.0, expected)) < 1e-13);
    }
}

TEST_CASE("collapse operators follow the calibration") {
    auto ld = loadShippedDevice("sc_fixed_5");
    REQUIRE(ld);
    const std::vector<std::uint32_t> qs{0};
    auto m = buildSystemModel(ld->device, ld->calibration, opts(qs));
    REQUIRE(m);
    REQUIRE(m->dimension() == 3);
    bool t1 = false, tphi = false;
    for (const auto& c : m->collapse) {
        if (c.name == "T1 q0") {
            t1 = true;
            // L = sqrt(gamma1 (nbar+1)) a, so |<0|L|1>|^2 = gamma1 (nbar+1) ~ 1/T1.
            const double rate = std::norm(c.op.at(0, 1));
            const double g1 = 1.0 / ld->calibration.qubits[0].t1.value.v;
            REQUIRE(rate == Approx(g1 * (1.0 + 2.0 * ld->calibration.qubits[0].thermalPopulation.value))
                                .epsilon(0.05));
        }
        if (c.name == "Tphi q0") tphi = true;
        REQUIRE(c.op.rows == 3);
    }
    REQUIRE(t1);
    REQUIRE(tphi);

    // Thermal excitation adds an upward jump; switching it off removes it.
    bool up = false;
    for (const auto& c : m->collapse)
        if (c.name == "Tth q0") up = true;
    REQUIRE(up);
    auto o = opts(qs);
    o.includeThermal = false;
    auto cold = buildSystemModel(ld->device, ld->calibration, o);
    REQUIRE(cold);
    for (const auto& c : cold->collapse) REQUIRE(c.name != "Tth q0");

    o = opts(qs);
    o.includeDecoherence = false;
    auto clean = buildSystemModel(ld->device, ld->calibration, o);
    REQUIRE(clean);
    REQUIRE(clean->collapse.empty());
}

TEST_CASE("an ion model carries the motional mode and heating") {
    auto ld = loadShippedDevice("ion_chain_11");
    REQUIRE(ld);
    const std::vector<std::uint32_t> qs{0, 1};
    auto o = opts(qs);
    o.fockCutoff = 4;
    auto m = buildSystemModel(ld->device, ld->calibration, o);
    if (!m) FAIL(m.error().format());
    // Two two-level ions plus one Fock mode.
    REQUIRE(m->siteDims == std::vector<std::uint32_t>{2, 2, 4});
    REQUIRE(m->dimension() == 16);
    REQUIRE(num::isHermitian(m->h0Dense()));
    // H0 holds the mode ladder at the gate-mode frequency.
    const double modeHz = ld->calibration.motional->axialModes[0].v;
    REQUIRE(m->h0.at(4, 4).real() == Approx(kTwoPi * modeHz).epsilon(1e-9)); // |n=1> of the mode
    bool ms = false, heat = false;
    for (const auto& d : m->drives)
        if (d.channel == "ms[0,1]") ms = true;
    for (const auto& c : m->collapse)
        if (c.name == "heating") heat = true;
    REQUIRE(ms);
    REQUIRE(heat);
}

TEST_CASE("the model refuses sizes beyond the pulse-level cap") {
    auto ld = loadShippedDevice("sc_heavyhex_27");
    REQUIRE(ld);
    const std::vector<std::uint32_t> big{0, 1, 2, 3, 4, 5};
    REQUIRE_FALSE(buildSystemModel(ld->device, ld->calibration, opts(big)).has_value());
    const std::vector<std::uint32_t> bad{999};
    auto r = buildSystemModel(ld->device, ld->calibration, opts(bad));
    REQUIRE_FALSE(r);
    REQUIRE(r.error().format().find("999") != std::string::npos);
    const std::vector<std::uint32_t> none{};
    REQUIRE_FALSE(buildSystemModel(ld->device, ld->calibration, opts(none)).has_value());
}

TEST_CASE("perturbed calibrations stay physical and deterministic") {
    auto ld = loadShippedDevice("sc_fixed_5");
    REQUIRE(ld);
    auto a = ld->calibration.perturbed(7);
    auto b = ld->calibration.perturbed(7);
    auto c = ld->calibration.perturbed(8);
    REQUIRE(a.qubits[0].t1.value.v == b.qubits[0].t1.value.v);
    REQUIRE(a.qubits[0].t1.value.v != c.qubits[0].t1.value.v);
    REQUIRE(a.validate().has_value());
    for (const auto& q : a.qubits) {
        REQUIRE(q.t1.value.v > 0.0);
        REQUIRE(q.t2star.value.v <= q.t2echo.value.v * (1.0 + 1e-9));
        REQUIRE(q.t2echo.value.v <= 2.0 * q.t1.value.v * (1.0 + 1e-9));
        REQUIRE(q.gateError1q.value > 0.0);
        REQUIRE(q.gateError1q.value < 1.0);
    }
}

TEST_CASE("gate lookups resolve through the calibration") {
    auto ld = loadShippedDevice("sc_fixed_5");
    REQUIRE(ld);
    const auto& cal = ld->calibration;
    const std::vector<std::uint32_t> q0{0};
    REQUIRE(cal.gateDuration("sx", q0).value().v == Approx(32e-9));
    REQUIRE(cal.gateDuration("rz", q0).value().v == Approx(0.0)); // virtual Z is free
    REQUIRE(cal.gateError("sx", q0).value() > 0.0);
    const std::vector<std::uint32_t> pair{0, 1};
    REQUIRE(cal.gateDuration("cx", pair).value().v == Approx(440e-9));
    REQUIRE(cal.gateError("cx", pair).value() > cal.gateError("sx", q0).value());
    const std::vector<std::uint32_t> far{0, 4};
    REQUIRE_FALSE(cal.gateDuration("cx", far).has_value()); // not a coupled pair
    auto idle = cal.idleParams(0);
    REQUIRE(idle);
    REQUIRE(idle->tphi.v > 0.0);
    REQUIRE(cal.maxT1().v >= idle->t1.v);
}

TEST_CASE("the calibration names the MS gate mode and the ion model uses it") {
    // ion_chain_11 gates on the axial COM, ion_chain_32 on the radial COM (T06 §6). The model
    // used to hard-code the axial mode and ignore motional.gate_mode.
    for (const auto& [id, axis] : std::vector<std::pair<std::string, std::string>>{
             {"ion_chain_11", "axial"}, {"ion_chain_32", "radial"}}) {
        INFO(id);
        auto ld = loadShippedDevice(id);
        REQUIRE(ld);
        REQUIRE(ld->calibration.motional.has_value());
        const auto& mo = *ld->calibration.motional;
        REQUIRE(mo.gateModeAxis == axis);
        REQUIRE(mo.gateModeIndex == 0u);
        REQUIRE(mo.gateModeParticipation.size() == ld->device.qubits.size());
        REQUIRE(mo.heatingQuantaPerS.has_value());
        const auto f = mo.gateModeFrequency();
        REQUIRE(f.has_value());
        const auto& list = axis == "radial" ? mo.radialModes : mo.axialModes;
        REQUIRE(f->v == Approx(list[0].v));

        const std::vector<std::uint32_t> qs{0, 1};
        SystemModelOptions o = opts(qs);
        o.includeDecoherence = false;
        o.fockCutoff = 4;
        auto m = buildSystemModel(ld->device, ld->calibration, o);
        REQUIRE(m);
        // H0 holds ω_m a†a with the qubits in their own frames: the first Fock step of the mode
        // (the most significant site) is exactly 2π f_mode.
        const auto h0 = m->h0.toDense();
        const std::size_t modeStride = 4; // two qubits below the mode site (little-endian)
        const double step = (h0(modeStride, modeStride) - h0(0, 0)).real();
        REQUIRE(step == Approx(2.0 * 3.14159265358979323846 * f->v).epsilon(1e-12));
    }
}

TEST_CASE("the MS drive operator is (eta_n/2) sigma (a + a^dagger) per ion (T06 (6.1))") {
    // Regression: the operator was (eta/4) S (a + a†) with one averaged eta — half the strength
    // that Pulse's envelope F(t) = 2 Omega cos(mu t) assumes, so XX(pi/2) would come out as XX(pi/8).
    auto ld = loadShippedDevice("ion_chain_11");
    REQUIRE(ld);
    const std::vector<std::uint32_t> qs{2, 7};
    SystemModelOptions o = opts(qs);
    o.includeDecoherence = false;
    o.fockCutoff = 4;
    auto m = buildSystemModel(ld->device, ld->calibration, o);
    REQUIRE(m);
    const DriveSpec* ms = nullptr;
    for (const auto& d : m->drives)
        if (d.channel == "ms[2,7]") ms = &d;
    REQUIRE(ms != nullptr);
    const double etaI = ld->calibration.qubits[2].lambDicke->value;
    const double etaJ = ld->calibration.qubits[7].lambDicke->value;
    const auto X = ms->inPhase.toDense();
    const auto Y = ms->quadrature.toDense();
    REQUIRE(num::isHermitian(X));
    REQUIRE(num::isHermitian(Y));
    // Basis index = q_i + 2 q_j + 4 n (little-endian; the mode is the last site).
    // <q_i=1, n=1| X |q_i=0, n=0> = (eta_i/2) <1|sigma_x|0> <1|a + a†|0> = eta_i/2.
    REQUIRE(std::abs(X(1 + 4, 0) - num::Complex(0.5 * etaI, 0.0)) < 1e-14);
    REQUIRE(std::abs(X(2 + 4, 0) - num::Complex(0.5 * etaJ, 0.0)) < 1e-14);
    // sigma_y = [[0, -i], [i, 0]], so <1|sigma_y|0> = +i.
    REQUIRE(std::abs(Y(1 + 4, 0) - num::Complex(0.0, 0.5 * etaI)) < 1e-14);
    REQUIRE(std::abs(Y(2 + 4, 0) - num::Complex(0.0, 0.5 * etaJ)) < 1e-14);
    // The second Fock step carries sqrt(2).
    REQUIRE(std::abs(X(1 + 8, 4) - num::Complex(0.5 * etaI * std::sqrt(2.0), 0.0)) < 1e-14);
    // No spin flip without a motional quantum exchanged.
    REQUIRE(std::abs(X(1, 0)) < 1e-15);
}
