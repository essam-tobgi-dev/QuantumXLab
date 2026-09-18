// Spec 15 §2, §10 — the `auto` backend policy table, pinned backends that cannot run, and the
// QL5xxx runtime diagnostics.
#include "RuntimeTestUtil.hpp"
#include "Data/Fidelity.hpp"
#include <numeric>

using namespace rtest;
using Catch::Approx;

namespace {
// A plan with `n` simulated qubits and nothing but terminal measurement, for the §2 table.
ProgramPlan flatPlan(std::uint32_t n, bool clifford) {
    ProgramPlan p;
    p.qubits.resize(n);
    std::iota(p.qubits.begin(), p.qubits.end(), 0u);
    p.toSim.assign(n, -1);
    for (std::uint32_t i = 0; i < n; ++i) p.toSim[i] = static_cast<std::int32_t>(i);
    p.cliffordOnly = clifford;
    p.measuredQubits = p.qubits;
    p.layout.bits = n;
    return p;
}

BackendRequest requestFor(const ProgramPlan& plan, bool noise, std::uint32_t shots) {
    BackendRequest r;
    r.plan = &plan;
    r.hasNoise = noise;
    r.shots = shots;
    return r;
}

std::string wide(std::uint32_t n) {
    std::string s = std::format("pragma qlab.layout physical\nbit[{}] c;\n", n);
    for (std::uint32_t q = 0; q < n; ++q) s += std::format("h ${};\n", q);
    for (std::uint32_t q = 0; q < n; ++q) s += std::format("c[{}] = measure ${};\n", q, q);
    return s;
}
} // namespace

TEST_CASE("the auto policy follows the spec 15 section 2 table") {
    const ProgramPlan small = flatPlan(2, false), medium = flatPlan(14, false), big = flatPlan(30, true);

    // Ideal and inside the memory cap: the state vector.
    auto ideal = chooseBackend(requestFor(small, false, 1024));
    REQUIRE(ideal);
    REQUIRE(ideal->kind == qsim::Kind::StateVector);
    REQUIRE(!ideal->reason.empty());
    REQUIRE(!ideal->stochasticUnravelling);

    // Noise, n ≤ 12, shots ≥ 256: one density-matrix evolution with exact Born sampling.
    auto dm = chooseBackend(requestFor(small, true, 1024));
    REQUIRE(dm);
    REQUIRE(dm->kind == qsim::Kind::DensityMatrix);
    REQUIRE(!dm->stochasticUnravelling);

    // Noise with fewer than 256 shots: trajectories are cheaper (one per shot).
    auto few = chooseBackend(requestFor(small, true, 64));
    REQUIRE(few);
    REQUIRE(few->kind == qsim::Kind::StateVector);
    REQUIRE(few->stochasticUnravelling);
    REQUIRE(few->cls == data::FidelityClass::Statistical);
    REQUIRE(few->reason.find("256") != std::string::npos);

    // Noise above 12 qubits: Monte-Carlo trajectories on the state vector.
    auto traj = chooseBackend(requestFor(medium, true, 4096));
    REQUIRE(traj);
    REQUIRE(traj->kind == qsim::Kind::StateVector);
    REQUIRE(traj->stochasticUnravelling);

    // Clifford + measurement above 28 qubits: the stabilizer.
    auto stab = chooseBackend(requestFor(big, false, 1024));
    REQUIRE(stab);
    REQUIRE(stab->kind == qsim::Kind::Stabilizer);
    // A non-Clifford circuit of the same width has nowhere to run: QL5011.
    const ProgramPlan huge = flatPlan(64, false);
    auto none = chooseBackend(requestFor(huge, false, 1024));
    REQUIRE_FALSE(none.has_value());
    REQUIRE(none.error().diagnosticId == "QL5011");
    REQUIRE(none.error().code == err::NoBackend);
}

TEST_CASE("a pinned backend that cannot run the program is a hard error, never a fallback") {
    const ProgramPlan plan = flatPlan(2, false);
    BackendRequest req = requestFor(plan, true, 1024);
    req.choice = BackendChoice::StateVector;
    auto pinned = chooseBackend(req);
    REQUIRE(pinned);
    REQUIRE(pinned->kind == qsim::Kind::StateVector); // pinned wins over the density-matrix row

    const ProgramPlan wideNonClifford = flatPlan(40, false);
    BackendRequest bad = requestFor(wideNonClifford, false, 1024);
    bad.choice = BackendChoice::StateVector;
    auto refused = chooseBackend(bad);
    REQUIRE_FALSE(refused.has_value());   // the selection error is returned verbatim, no silent switch

    BackendRequest stab = requestFor(wideNonClifford, false, 1024);
    stab.choice = BackendChoice::Stabilizer;
    REQUIRE_FALSE(chooseBackend(stab).has_value()); // non-Clifford on the stabilizer
}

TEST_CASE("QL5010: the Lindblad cap on qubits and levels") {
    ProgramPlan plan = flatPlan(6, false);
    BackendRequest req = requestFor(plan, true, 1);
    req.pulseLevel = true;
    req.levels = 3;
    auto over = chooseBackend(req);
    REQUIRE_FALSE(over.has_value());
    REQUIRE(over.error().diagnosticId == "QL5010");
    REQUIRE(over.error().code == err::LindbladCap);

    const ProgramPlan five = flatPlan(5, false);
    BackendRequest ok = requestFor(five, true, 1);
    ok.pulseLevel = true;
    ok.levels = 3;
    auto fits = chooseBackend(ok);
    REQUIRE(fits);
    REQUIRE(fits->kind == qsim::Kind::Lindblad);
}

TEST_CASE("QL5040: the shot count must be inside [1, 1e7]") {
    Lab& l = lab("sc_fixed_5");
    const Lab::Job job = l.compile(readAsset("Programs/Examples/Basics/bell.qasm"));
    RunOptions options;
    options.shots = 0;
    auto zero = l.session.runSync(l.request(job, options));
    REQUIRE_FALSE(zero.has_value());
    REQUIRE(zero.error().diagnosticId == "QL5040");
    options.shots = RunOptions::kMaxShots + 1;
    REQUIRE(l.session.runSync(l.request(job, options)).error().diagnosticId == "QL5040");
}

TEST_CASE("QL5012: a state that does not fit the memory budget is refused, never swapped to disk") {
    // 25 sites at 3 levels is 16·3^25 ≈ 13 TB. The runtime refuses before it allocates anything
    // (spec 15 §3.3); the same 25 qubits at 2 levels are 537 MB and run.
    Lab& l = lab("sc_heavyhex_27");
    const Lab::Job job = l.compile(source(wide(25)));
    const compiler::CompiledProgram* program = l.session.compiled(job.handle);
    REQUIRE(program != nullptr);
    auto plan = planProgram(program->circuit);
    REQUIRE(plan);
    REQUIRE(plan->nQubits() == 25);

    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 4;
    options.levels = 3;
    ExecutionInput in;
    in.program = program;
    in.plan = &*plan;
    in.device = l.session.device();
    in.calibration = l.session.calibration();
    in.backend = qsim::Kind::StateVector;
    in.shots = 4;
    in.options = &options;
    auto out = execute(in);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().diagnosticId == "QL5012");
    REQUIRE(out.error().code == err::MemoryBudget);
    REQUIRE(out.error().message.find("GiB") != std::string::npos);

    options.levels = 2;
    options.cadence = SnapshotCadence::None;
    auto fits = execute(in);
    REQUIRE(fits);
    REQUIRE(fits->memory.size() == 4);
}

TEST_CASE("QL5001: an input with no value at run time") {
    Lab& l = lab("sc_fixed_5");
    compiler::CompileOptions copts;
    copts.inputs["theta"] = 0.4;
    const Lab::Job job = l.compile(source("input float theta;\nqubit[1] q;\nbit c;\n"
                                          "ry(theta) q[0];\nc = measure q[0];\n"),
                                   copts);
    // The compile bound `theta`; a run that does not (a sweep would have to rebind it) is QL5001.
    RunRequest request = l.request(job, RunOptions{});
    request.compileOptions = compiler::CompileOptions{};
    auto out = l.session.runSync(request);
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().diagnosticId == "QL5001");
    REQUIRE(out.error().message.find("theta") != std::string::npos);
    // With the binding it runs and reproduces sin²(θ/2).
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 4096;
    options.seed = 8;
    const RunResult r = l.session.runSync(l.request(job, options)).value();
    const double expected = std::sin(0.2) * std::sin(0.2);
    REQUIRE(std::abs(r.probability("1") - expected) < 5.0 * sigma(expected, 4096));
}

TEST_CASE("QL5030: the sweep grid is bounded by 4 axes and 1e5 points") {
    SweepGrid grid;
    for (int a = 0; a < 4; ++a) {
        SweepAxis axis;
        axis.input = std::format("x{}", a);
        axis.values.assign(20, 0.0);
        grid.axes.push_back(std::move(axis));
    }
    REQUIRE(grid.points() == 160000);
    Lab& l = lab("sc_fixed_5");
    const Lab::Job job = l.compile(readAsset("Programs/Examples/Basics/bell.qasm"));
    RunOptions options;
    options.sweep = grid;
    auto out = l.session.runSync(l.request(job, options));
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().diagnosticId == "QL5030");
    // A fifth axis is refused as well.
    grid.axes.resize(2);
    for (SweepAxis& a : grid.axes) a.values.assign(2, 0.0);
    for (int a = 0; a < 3; ++a) grid.axes.push_back(SweepAxis{std::format("y{}", a), {}, {0.0, 1.0}});
    REQUIRE(grid.axes.size() > SweepGrid::kMaxAxes);
    options.sweep = grid;
    REQUIRE(l.session.runSync(l.request(job, options)).error().diagnosticId == "QL5030");
}

TEST_CASE("QL5050: a custom noise file that cannot be read or parsed") {
    Lab& l = lab("sc_fixed_5");
    const Lab::Job job = l.compile(readAsset("Programs/Examples/Basics/bell.qasm"));
    RunOptions options;
    options.noise = NoiseSource::Custom;
    options.noiseFile = "no/such/noise.json";
    auto out = l.session.runSync(l.request(job, options));
    REQUIRE_FALSE(out.has_value());
    REQUIRE(out.error().diagnosticId == "QL5050");
    REQUIRE(out.error().code == err::BadNoiseFile);
}

TEST_CASE("the run header names the backend and why it was chosen") {
    Lab& l = lab("sc_fixed_5");
    RunOptions options;
    options.shots = 1024;
    options.seed = 4;
    const RunResult noisy = l.run(readAsset("Programs/Examples/Basics/bell.qasm"), options);
    REQUIRE(noisy.backend == qsim::Kind::DensityMatrix);
    REQUIRE(!noisy.backendReason.empty());
    REQUIRE(noisy.device == "sc_fixed_5");
    REQUIRE(!noisy.calibrationTimestamp.empty());
    REQUIRE(noisy.programHash != 0);

    options.noise = NoiseSource::Ideal;
    const RunResult ideal = l.run(readAsset("Programs/Examples/Basics/bell.qasm"), options);
    REQUIRE(ideal.backend == qsim::Kind::StateVector);
    REQUIRE(ideal.backendClass == data::FidelityClass::Exact);
    REQUIRE(ideal.programHash == noisy.programHash);
}
