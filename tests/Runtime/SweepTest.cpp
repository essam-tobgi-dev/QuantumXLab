// Spec 15 §5 — sweeps: an `input` variable × grid gives a tensor of per-point summaries; a swept
// Rabi amplitude reproduces P₁(a) = sin²(πa/2) point by point (spec 22 §4 calibration fits).
#include "RuntimeTestUtil.hpp"
#include <cmath>

using namespace rtest;
using Catch::Approx;

namespace {
constexpr const char* kRabi = "input float amp = 0.0;\nqubit[1] q;\nbit c;\n"
                              "rx(3.141592653589793 * amp) q[0];\nc = measure q[0];\n";

SweepGrid rabiGrid(double from, double to, double step) {
    SweepAxis axis;
    axis.input = "amp";
    axis.unit = "a_pi";
    for (double a = from; a <= to + 1e-9; a += step) axis.values.push_back(a);
    SweepGrid grid;
    grid.axes.push_back(std::move(axis));
    return grid;
}
} // namespace

TEST_CASE("a Rabi-amplitude sweep reproduces the expected cosine") {
    Lab& l = lab("sc_fixed_5");
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 2048;
    options.seed = 77;
    options.sweep = rabiGrid(0.0, 2.0, 0.1);
    const RunResult r = l.run(source(kRabi), options);

    REQUIRE(r.sweep.has_value());
    const SweepResult& s = *r.sweep;
    REQUIRE(s.axes.size() == 1);
    REQUIRE(s.axes[0].input == "amp");
    REQUIRE(s.shape() == std::vector<std::size_t>{21});
    REQUIRE(s.points.size() == 21);

    for (const SweepPoint& p : s.points) {
        REQUIRE(p.coords.size() == 1);
        const double a = p.coords[0];
        const double expected = std::sin(0.5 * kPi * a) * std::sin(0.5 * kPi * a); // P₁ = sin²(πa/2)
        INFO("amp = " << a << ": measured " << p.p1 << ", expected " << expected);
        REQUIRE(p.counts.total() == 2048);
        REQUIRE(std::abs(p.p1 - expected) < 5.0 * sigma(std::max(expected, 1e-4), 2048) + 1e-9);
        REQUIRE(p.p1Stderr >= 0.0);
    }
    // The extremes are exact: a = 0 and a = 2 return to |0⟩, a = 1 is a full π pulse.
    REQUIRE(s.points.front().p1 == Approx(0.0).margin(1e-12));
    REQUIRE(s.points.back().p1 == Approx(0.0).margin(1e-12));
    REQUIRE(s.points[10].p1 == Approx(1.0).margin(1e-12));
    REQUIRE(s.points[5].p1 == Approx(0.5).margin(5.0 * sigma(0.5, 2048)));
}

TEST_CASE("the sweep is reproducible from one seed and each point uses that seed plus its index") {
    Lab& l = lab("sc_fixed_5");
    RunOptions options;
    options.noise = NoiseSource::Calibrated;
    options.shots = 256;
    options.seed = 500;
    options.sweep = rabiGrid(0.2, 0.8, 0.2);
    const std::string text = source(kRabi);
    const RunResult a = l.run(text, options);
    const RunResult b = l.run(text, options);
    REQUIRE(a.sweep->points.size() == 4);
    for (std::size_t k = 0; k < a.sweep->points.size(); ++k)
        REQUIRE(a.sweep->points[k].counts.raw() == b.sweep->points[k].counts.raw());

    // Point k is the same run as a single run at seed + k with that binding.
    RunOptions single = options;
    single.sweep.reset();
    single.seed = 502;
    compiler::CompileOptions copts;
    copts.inputs["amp"] = 0.6; // the third point, index 2
    const RunResult direct = l.run(text, single, copts);
    REQUIRE(direct.counts.raw() == a.sweep->points[2].counts.raw());
}

TEST_CASE("a two-axis grid is a row-major tensor with axis 0 slowest") {
    SweepGrid grid;
    grid.axes.push_back(SweepAxis{"x", "", {0.0, 1.0, 2.0}});
    grid.axes.push_back(SweepAxis{"y", "", {10.0, 20.0}});
    REQUIRE(grid.points() == 6);
    REQUIRE(grid.at(0) == std::vector<double>{0.0, 10.0});
    REQUIRE(grid.at(1) == std::vector<double>{0.0, 20.0});
    REQUIRE(grid.at(2) == std::vector<double>{1.0, 10.0});
    REQUIRE(grid.at(5) == std::vector<double>{2.0, 20.0});

    Lab& l = lab("sc_fixed_5");
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 512;
    options.seed = 9;
    options.sweep = grid;
    const RunResult r = l.run(source("input float x = 0.0;\ninput float y = 0.0;\nqubit[1] q;\nbit c;\n"
                                     "rx(0.5 * x) q[0];\nrz(0.01 * y) q[0];\nc = measure q[0];\n"),
                              options);
    REQUIRE(r.sweep->shape() == std::vector<std::size_t>{3, 2});
    REQUIRE(r.sweep->points.size() == 6);
    // rz after rx does not change P₁, so both y values agree at each x; x does change it.
    for (std::size_t x = 0; x < 3; ++x) {
        const double expected = std::sin(0.25 * static_cast<double>(x)) * std::sin(0.25 * static_cast<double>(x));
        for (std::size_t y = 0; y < 2; ++y) {
            const SweepPoint& p = r.sweep->points[x * 2 + y];
            REQUIRE(p.coords[0] == Approx(static_cast<double>(x)).margin(1e-12));
            REQUIRE(std::abs(p.p1 - expected) < 5.0 * sigma(std::max(expected, 1e-4), 512) + 1e-9);
        }
    }
}

TEST_CASE("a program's pragma qlab.sweep defines the grid when the run dialog does not") {
    Lab& l = lab("sc_fixed_5");
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 1024;
    options.seed = 13;
    const RunResult r = l.run(source("input float amp = 0.0;\npragma qlab.sweep amp from 0.0 to 1.0 step 0.25\n"
                                     "qubit[1] q;\nbit c;\nrx(3.141592653589793 * amp) q[0];\nc = measure q[0];\n"),
                              options);
    REQUIRE(r.sweep.has_value());
    REQUIRE(r.sweep->axes.size() == 1);
    REQUIRE(r.sweep->axes[0].input == "amp");
    REQUIRE(r.sweep->points.size() == 5);
    REQUIRE(r.sweep->points[4].p1 == Approx(1.0).margin(1e-12));
    // The sweep does not replace the run: the base result is still there with its estimate.
    REQUIRE(r.counts.total() == 1024);
    REQUIRE(r.estimate.wallTime.valueS > 0.0);
}
