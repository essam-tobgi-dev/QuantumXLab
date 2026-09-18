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

namespace {
struct Synth { std::vector<double> x, y; };
Synth synth(const FitModel& m, std::vector<double> truth, double x0, double x1, int n, double noise, std::uint64_t seed) {
    core::Random rng(seed); Synth s;
    for (int i = 0; i < n; ++i) { double x = x0 + (x1 - x0) * i / (n - 1); s.x.push_back(x); s.y.push_back(m.eval(truth, x) + rng.normal(0, noise)); }
    return s;
}
void checkFit(const std::string& id, std::vector<double> truth, double x0, double x1, double noise, double relTol = 0.0) {
    auto m = makeModel(id); REQUIRE(m);
    auto s = synth(*m, truth, x0, x1, 200, noise, 11);
    auto r = fitModel(*m, s.x, s.y); REQUIRE(r); REQUIRE(r->converged);
    for (std::size_t i = 0; i < truth.size(); ++i) {
        INFO(id << " param " << r->paramNames[i] << " got " << r->beta[i] << " truth " << truth[i] << " sigma " << r->sigma[i]);
        double tol = std::max(3.0 * r->sigma[i], relTol * std::abs(truth[i]));
        if (r->paramNames[i] == "phi") { double d = std::remainder(r->beta[i] - truth[i], 2 * std::numbers::pi); REQUIRE(std::abs(d) <= tol + 1e-9); }
        else REQUIRE(std::abs(r->beta[i] - truth[i]) <= tol + 1e-12);
    }
}
} // namespace

TEST_CASE("exp_decay recovers T1 = 100 us") { checkFit("exp_decay", {0.9, 100e-6, 0.05}, 0, 500e-6, 0.01); }
TEST_CASE("ramsey recovers delta = 0.5 MHz, T2* = 20 us") { checkFit("ramsey", {0.45, 20e-6, 0.5e6, 0.1, 0.5}, 0, 40e-6, 0.01, 0.02); }
TEST_CASE("echo exponential and gaussian") {
    checkFit("echo", {0.5, 30e-6, 0.5}, 0, 100e-6, 0.01);
    checkFit("echo_gauss", {0.5, 30e-6, 0.5}, 0, 100e-6, 0.01);
}
TEST_CASE("rabi_amp recovers a_pi") {
    checkFit("rabi_amp", {-0.45, 0.31, 0.5}, 0, 1.0, 0.01, 0.02);
    auto m = makeModel("rabi_amp"); std::vector<double> b{-0.45, 0.31, 0.5};
    auto d = m->derived(b); REQUIRE(d[0].first == "a_pi2"); REQUIRE(d[0].second == Approx(0.155));
}
TEST_CASE("rabi_time recovers frequency") { checkFit("rabi_time", {0.45, 5e6, 0.0, 2e-6, 0.5}, 0, 1e-6, 0.01, 0.02); }
TEST_CASE("lorentzian and gaussian peaks") {
    checkFit("lorentzian", {1.0, 5.1e9, 2e6, 0.1}, 5.09e9, 5.11e9, 0.01, 0.02);
    checkFit("gaussian", {1.0, 5.1e9, 1e6, 0.1}, 5.09e9, 5.11e9, 0.01, 0.02);
}
TEST_CASE("rb_decay recovers p = 0.99 and error rate") {
    auto m = makeModel("rb_decay"); REQUIRE(m);
    core::Random rng(3); std::vector<double> x, y;
    for (int mlen : {1, 2, 4, 8, 16, 32, 64, 128, 256}) for (int rep = 0; rep < 5; ++rep) { x.push_back(mlen); y.push_back(0.5 * std::pow(0.99, mlen) + 0.5 + rng.normal(0, 0.005)); }
    auto r = fitModel(*m, x, y); REQUIRE(r); REQUIRE(r->converged);
    REQUIRE(r->param("p") == Approx(0.99).margin(0.003));
    REQUIRE(rbErrorFromP(0.99, 1) == Approx(0.005));
    REQUIRE(rbErrorFromP(0.99, 2) == Approx(0.0075));
    bool found = false; for (auto& [k, v] : r->derived) if (k == "r") { found = true; REQUIRE(v == Approx(0.005).margin(0.0015)); }
    REQUIRE(found);
}
TEST_CASE("linear and fixed parameters and bounds") {
    auto m = makeModel("linear"); std::vector<double> x{0, 1, 2, 3}, y{1, 3, 5, 7};
    auto r = fitModel(*m, x, y); REQUIRE(r); REQUIRE(r->param("a") == Approx(1.0).margin(1e-9)); REQUIRE(r->param("b") == Approx(2.0).margin(1e-9));
    FitOptions o; o.initial = std::vector<double>{0.0, 2.0}; o.fixed = {true, false};
    auto r2 = fitModel(*m, x, y, {}, o); REQUIRE(r2); REQUIRE(r2->param("a") == 0.0);
    REQUIRE(r2->param("b") == Approx(34.0 / 14.0).margin(1e-6));
}
TEST_CASE("dominant frequency guess") {
    std::vector<double> x, y; for (int i = 0; i < 256; ++i) { x.push_back(i * 1e-8); y.push_back(std::cos(2 * std::numbers::pi * 3e6 * x.back())); }
    REQUIRE(dominantFrequency(x, y) == Approx(3e6).epsilon(0.05));
}
