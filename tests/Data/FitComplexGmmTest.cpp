#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Data/Data.hpp"
#include "Core/Random.hpp"
#include <cmath>
#include <numbers>
using namespace qlab;
using namespace qlab::data;
using namespace qlab::data::fit;
using Catch::Approx;

TEST_CASE("resonator_notch recovers f_r, Q_i, Q_c at 40 dB SNR") {
    ResonatorNotchModel m;
    const double fr = 7.1e9, Qi = 2e5, Qc = 5e4, phi = 0.2;
    const double Ql = 1.0 / (1.0 / Qi + std::cos(phi) / Qc);
    std::vector<double> truth{fr, Ql, Qc, phi, 0.8, 0.3, 40e-9};
    core::Random rng(5);
    std::vector<double> f, re, im;
    const double span = 8 * fr / Ql;
    const double noise = 0.8 * std::pow(10.0, -40.0 / 20.0);
    for (int i = 0; i < 400; ++i) {
        double x = fr - span / 2 + span * i / 399.0;
        Complex s = m.evalC(truth, x);
        f.push_back(x); re.push_back(s.real() + rng.normal(0, noise)); im.push_back(s.imag() + rng.normal(0, noise));
    }
    // Analytic Jacobian vs finite differences at the truth.
    {
        std::vector<Complex> J(7);
        REQUIRE(m.jacobianC(truth, fr + 1e4, J));
        for (std::size_t j = 0; j < 7; ++j) {
            std::vector<double> bp = truth, bm = truth;
            double h = j == 6 ? 1e-15 : std::max(1e-6 * std::abs(truth[j]), 1e-12); // tau: 2πf h must stay ≪ 1
            bp[j] += h; bm[j] -= h;
            Complex fd = (m.evalC(bp, fr + 1e4) - m.evalC(bm, fr + 1e4)) / (2 * h);
            INFO("param " << j);
            REQUIRE(std::abs(fd - J[j]) <= 1e-4 * std::max(1.0, std::abs(J[j])));
        }
    }
    auto r = fitModel(m, f, re, {}, {}, im);
    REQUIRE(r); REQUIRE(r->converged);
    REQUIRE(std::abs(r->param("f_r") - fr) / fr < 1e-7);
    REQUIRE(r->param("Q_c") == Approx(Qc).epsilon(0.02));
    double QiFit = 0; for (auto& [k, v] : r->derived) if (k == "Q_i") QiFit = v;
    REQUIRE(QiFit == Approx(Qi).epsilon(0.02));
}

TEST_CASE("gmm2 separates two IQ blobs and estimates assignment error") {
    core::Random rng(9);
    std::vector<double> x, y; std::vector<int> lab;
    const double sep = 4.0, sigma = 1.0;
    for (int i = 0; i < 4000; ++i) {
        int s = i % 2;
        x.push_back((s ? sep : 0.0) + rng.normal(0, sigma));
        y.push_back(0.5 * sep * s + rng.normal(0, sigma));
        lab.push_back(s);
    }
    auto g = fitGmm2(x, y);
    REQUIRE(g); REQUIRE(g->converged);
    REQUIRE(g->comp[0].mx == Approx(0.0).margin(0.15));
    REQUIRE(g->comp[1].mx == Approx(sep).margin(0.15));
    REQUIRE(g->comp[1].my == Approx(0.5 * sep).margin(0.15));
    double expectedSnr = std::sqrt(sep * sep + 0.25 * sep * sep) / sigma;
    REQUIRE(g->snr == Approx(expectedSnr).epsilon(0.08));
    REQUIRE(g->assignmentError == Approx(0.5 * std::erfc(expectedSnr / (2 * std::numbers::sqrt2))).epsilon(0.3));
    auto M = assignmentMatrix(x, y, lab, *g);
    REQUIRE(M[0][0] + M[0][1] == Approx(1.0));
    REQUIRE(M[0][0] > 0.95); REQUIRE(M[1][1] > 0.95);
    Gmm2Options o; o.labels = lab; o.sharedCovariance = false;
    auto g2 = fitGmm2(x, y, o); REQUIRE(g2); REQUIRE(g2->comp[1].mx == Approx(sep).margin(0.15));
    REQUIRE_FALSE(fitGmm2(std::vector<double>{1, 2}, std::vector<double>{1, 2}).has_value());
}
