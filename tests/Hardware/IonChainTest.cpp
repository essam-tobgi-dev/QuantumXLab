// Spec 09 §5.4 — linear-chain equilibrium, normal modes and Lamb-Dicke factors (T06 §1, §3).
#include "Hardware/Hardware.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace qlab;
using namespace qlab::hw;
using Catch::Approx;
using units::Frequency;

TEST_CASE("equilibrium positions match the analytic small-N solutions") {
    // N = 2: u = ±(1/2)^(2/3) / ... the dimensionless solution is ±(1/4)^(1/3) = ±0.62996.
    auto u2 = ion::equilibriumPositions(2);
    REQUIRE(u2);
    REQUIRE(u2->size() == 2);
    const double a2 = std::pow(0.25, 1.0 / 3.0);
    REQUIRE((*u2)[0] == Approx(-a2).epsilon(1e-9));
    REQUIRE((*u2)[1] == Approx(a2).epsilon(1e-9));

    // N = 3: outer ions at ±(5/4)^(1/3), centre at 0.
    auto u3 = ion::equilibriumPositions(3);
    REQUIRE(u3);
    const double a3 = std::pow(1.25, 1.0 / 3.0);
    REQUIRE((*u3)[0] == Approx(-a3).epsilon(1e-9));
    REQUIRE((*u3)[1] == Approx(0.0).margin(1e-12));
    REQUIRE((*u3)[2] == Approx(a3).epsilon(1e-9));

    // Chains stay ordered and symmetric about the centre for larger N.
    auto u11 = ion::equilibriumPositions(11);
    REQUIRE(u11);
    for (std::size_t k = 1; k < u11->size(); ++k)
        REQUIRE((*u11)[k] > (*u11)[k - 1]);
    REQUIRE((*u11)[0] == Approx(-(*u11)[10]).epsilon(1e-6));
    REQUIRE((*u11)[5] == Approx(0.0).margin(1e-9));
}

TEST_CASE("axial normal modes reproduce the analytic ratios") {
    ion::ChainParams p;
    p.omegaZ = Frequency(0.3e6);
    p.omegaR = Frequency(3.0e6);

    // N = 2: COM at omega_z, stretch at sqrt(3) omega_z (T06 §1).
    p.count = 2;
    auto m2 = ion::normalModes(p);
    REQUIRE(m2);
    REQUIRE(m2->axial.size() == 2);
    REQUIRE(m2->axial[0].v / p.omegaZ.v == Approx(1.0).epsilon(1e-9));
    REQUIRE(m2->axial[1].v / p.omegaZ.v == Approx(std::sqrt(3.0)).epsilon(1e-9));

    // N = 3: 1, sqrt(3), sqrt(29/5).
    p.count = 3;
    auto m3 = ion::normalModes(p);
    REQUIRE(m3);
    REQUIRE(m3->axial.size() == 3);
    REQUIRE(m3->axial[0].v / p.omegaZ.v == Approx(1.0).epsilon(1e-9));
    REQUIRE(m3->axial[1].v / p.omegaZ.v == Approx(std::sqrt(3.0)).epsilon(1e-9));
    REQUIRE(m3->axial[2].v / p.omegaZ.v == Approx(std::sqrt(29.0 / 5.0)).epsilon(1e-9));

    // The COM mode has every ion moving together with equal amplitude 1/sqrt(N).
    for (std::size_t i = 0; i < 3; ++i)
        REQUIRE(std::abs(m3->axialVectors(i, 0)) == Approx(1.0 / std::sqrt(3.0)).epsilon(1e-9));

    // Radial modes are descending with the COM highest and below omega_r.
    REQUIRE(m3->radial[0].v <= p.omegaR.v * (1.0 + 1e-9));
    REQUIRE(m3->radial[0].v > m3->radial[2].v);
}

TEST_CASE("mode frequencies of the shipped chains match their calibration") {
    auto ld = loadShippedDevice("ion_chain_11");
    REQUIRE(ld);
    ion::ChainParams p;
    p.count = 11;
    p.omegaZ = ld->device.motionalModes->omegaZ;
    p.omegaR = ld->device.motionalModes->omegaR;
    auto modes = ion::normalModes(p);
    REQUIRE(modes);
    const auto& cal = *ld->calibration.motional;
    REQUIRE(cal.axialModes.size() == modes->axial.size());
    for (std::size_t k = 0; k < modes->axial.size(); ++k) {
        INFO("axial mode " << k);
        REQUIRE(modes->axial[k].v == Approx(cal.axialModes[k].v).epsilon(1e-5));
    }
    for (std::size_t k = 0; k < modes->u.size(); ++k)
        REQUIRE(modes->u[k] == Approx(cal.equilibriumPositions[k]).margin(1e-5));
}

TEST_CASE("Lamb-Dicke parameter scales as 1/sqrt(m omega)") {
    ion::ChainParams p;
    p.count = 2;
    auto m = ion::normalModes(p);
    REQUIRE(m);
    const double eta = m->etaAxial[0][0];
    REQUIRE(eta > 0.0);
    REQUIRE(eta < 1.0); // Lamb-Dicke regime
    // Doubling the trap frequency lowers eta by sqrt(2).
    ion::ChainParams p2 = p;
    p2.omegaZ = Frequency(p.omegaZ.v * 2.0);
    auto m2 = ion::normalModes(p2);
    REQUIRE(m2);
    REQUIRE(m2->etaAxial[0][0] == Approx(eta / std::sqrt(2.0)).epsilon(1e-9));
}

TEST_CASE("zigzag stability follows the T06 §1.2 criterion") {
    REQUIRE(ion::linearChainStable(11, Frequency(0.3e6), Frequency(3.0e6)));
    REQUIRE_FALSE(ion::linearChainStable(60, Frequency(0.3e6), Frequency(0.5e6)));
}

TEST_CASE("spin-motion operators have the expected structure") {
    auto ops = ion::spinMotionOperators(2, Frequency(1e6), 4);
    const std::size_t dim = 2 * 2 * 4;
    REQUIRE(ops.H0.rows == dim);
    REQUIRE(ops.a.rows == dim);
    REQUIRE(ops.sigmaX.size() == 2);
    REQUIRE(ops.sigmaZ.size() == 2);
    // [a, a†] = 1 on the truncated space except in the top Fock level.
    auto comm = num::add(num::matmul(ops.a, ops.adag), num::matmul(ops.adag, ops.a), 1.0, -1.0);
    REQUIRE(comm.at(0, 0).real() == Approx(1.0));
    REQUIRE(num::isHermitian(ops.H0.toDense()));
}
