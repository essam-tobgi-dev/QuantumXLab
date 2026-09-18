// Spec 15 §1, §3 — the session: device and backend selection, asynchronous cancellable compiles and
// runs on the job system, the events the UI subscribes to, the history, and the partial result of a
// cancelled run (QL5060).
#include "RuntimeTestUtil.hpp"
#include <chrono>
#include <thread>

using namespace rtest;
using Catch::Approx;
using Clock = std::chrono::steady_clock;

namespace {
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// Spin until `ready()` or `budgetMs` elapse; returns the elapsed milliseconds.
template <class F> double waitUntil(F ready, double budgetMs) {
    const auto start = Clock::now();
    while (!ready() && msSince(start) < budgetMs) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return msSince(start);
}
} // namespace

TEST_CASE("the session holds one device and one backend choice and reports both") {
    core::EventBus bus;
    Session session(&bus);
    std::vector<std::string> devices;
    std::vector<BackendChoice> backends;
    auto d = bus.subscribe<DeviceSelected>([&](const DeviceSelected& e) { devices.push_back(e.device); });
    auto b = bus.subscribe<BackendSelected>([&](const BackendSelected& e) { backends.push_back(e.choice); });

    REQUIRE(session.device() == nullptr);
    REQUIRE(session.selectDevice("sc_fixed_5").has_value());
    REQUIRE(session.device()->id == "sc_fixed_5");
    REQUIRE(!session.calibration()->timestamp.empty());
    REQUIRE(session.selectDevice("ion_chain_11").has_value());
    REQUIRE(session.device()->id == "ion_chain_11");
    REQUIRE_FALSE(session.selectDevice("no_such_device").has_value());
    REQUIRE(session.backendChoice() == BackendChoice::Auto);
    session.selectBackend(BackendChoice::Stabilizer);
    REQUIRE(session.backendChoice() == BackendChoice::Stabilizer);

    bus.drain();
    REQUIRE(devices == std::vector<std::string>{"sc_fixed_5", "ion_chain_11"});
    REQUIRE(backends == std::vector<BackendChoice>{BackendChoice::Stabilizer});
    REQUIRE(backendChoiceName(BackendChoice::Stabilizer) == "stabilizer");
    REQUIRE(noiseSourceName(NoiseSource::Calibrated) == "calibrated");
    REQUIRE(snapshotCadenceName(SnapshotCadence::Layer) == "layer");
}

TEST_CASE("compiling and running are asynchronous, post their events and fill the history") {
    core::JobSystem jobs(2);
    core::EventBus bus;
    Session session(&bus, &jobs);
    REQUIRE(session.selectDevice("sc_fixed_5").has_value());

    int started = 0, finished = 0, progress = 0;
    RunFinished lastFinish{};
    RunStarted lastStart{};
    auto s1 = bus.subscribe<CompileFinished>([&](const CompileFinished& e) { REQUIRE(e.ok); });
    auto s2 = bus.subscribe<RunStarted>([&](const RunStarted& e) { ++started; lastStart = e; });
    auto s3 = bus.subscribe<RunProgress>([&](const RunProgress&) { ++progress; });
    auto s4 = bus.subscribe<RunFinished>([&](const RunFinished& e) { ++finished; lastFinish = e; });

    auto id = session.loadProgram(readAsset("Programs/Examples/Protocols/teleportation.qasm"), "tele.qasm");
    REQUIRE(id);
    REQUIRE(session.program(*id) != nullptr);
    REQUIRE(session.source(*id).size() > 0);
    auto handle = session.compile(*id);
    REQUIRE(handle);
    REQUIRE(session.waitCompile(*handle).has_value());
    REQUIRE(session.compileDone(*handle));
    REQUIRE(session.compiled(*handle) != nullptr);

    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 2048;
    options.seed = 17;
    auto run = session.run(*handle, options);
    REQUIRE(run);
    REQUIRE(session.wait(*run).has_value());
    REQUIRE(session.done(*run));
    const RunResult* r = session.result(*run);
    REQUIRE(r != nullptr);
    REQUIRE(r->shotsCompleted == 2048);
    REQUIRE_FALSE(r->partial);
    REQUIRE(r->wallTime.count() > 0);

    bus.drain();
    REQUIRE(started == 1);
    REQUIRE(finished == 1);
    REQUIRE(progress > 0);
    REQUIRE(lastFinish.ok);
    REQUIRE(lastFinish.shots == 2048);
    // The run header carries the chosen backend, its reason and the seed actually used (§2).
    REQUIRE(lastStart.device == "sc_fixed_5");
    REQUIRE(lastStart.backend == r->backend);
    REQUIRE(lastStart.backendReason == r->backendReason);
    REQUIRE(lastStart.seed == r->seed);
    REQUIRE(lastStart.shots == 2048);

    const std::vector<RunRecord> history = session.history();
    REQUIRE(history.size() == 1);
    REQUIRE(history[0].programHash == r->programHash);
    REQUIRE(history[0].device == "sc_fixed_5");
    REQUIRE(history[0].shots == 2048);
    REQUIRE(history[0].seed == 17);
    REQUIRE(history[0].backend == "StateVector");
    REQUIRE_FALSE(history[0].partial);
    // A second run appends.
    auto again = session.run(*handle, options);
    REQUIRE(again);
    REQUIRE(session.wait(*again).has_value());
    REQUIRE(session.history().size() == 2);
    // The result is not published before the run finishes for a handle that does not exist.
    REQUIRE(session.result(RunHandle{9999}) == nullptr);
    REQUIRE_FALSE(session.done(RunHandle{9999}));
}

TEST_CASE("cancelling a long run stops it within 100 ms and keeps the partial result") {
    core::JobSystem jobs(2);
    core::EventBus bus;
    Session session(&bus, &jobs);
    REQUIRE(session.selectDevice("sc_fixed_5").has_value());
    auto id = session.loadProgram(readAsset("Programs/Examples/Protocols/teleportation.qasm"), "tele.qasm");
    REQUIRE(id);
    auto handle = session.compile(*id);
    REQUIRE(handle);
    REQUIRE(session.waitCompile(*handle).has_value());

    RunOptions options;
    options.noise = NoiseSource::Calibrated;   // per-shot execution on the density matrix: slow on purpose
    options.shots = 1'000'000;
    options.seed = 5;
    options.computeEstimate = false;
    auto run = session.run(*handle, options);
    REQUIRE(run);
    // Let it get going, then cancel and time the stop.
    waitUntil([&] { return session.done(*run); }, 60.0);
    REQUIRE_FALSE(session.done(*run));
    const auto cancelled = Clock::now();
    session.cancel(*run);
    const double stopMs = waitUntil([&] { return session.done(*run); }, 2000.0);
    INFO("stopped after " << stopMs << " ms");
    REQUIRE(session.done(*run));
    REQUIRE(stopMs < 100.0);
    REQUIRE(msSince(cancelled) < 200.0);

    const RunResult* r = session.result(*run);
    REQUIRE(r != nullptr);
    REQUIRE(r->partial);
    REQUIRE(r->shotsCompleted > 0);
    REQUIRE(r->shotsCompleted < options.shots);
    REQUIRE(r->memory.size() == r->shotsCompleted);
    REQUIRE(r->counts.total() == r->shotsCompleted);
    bool warned = false;
    for (const lang::Diagnostic& d : r->diagnostics) warned = warned || d.id() == "QL5060";
    REQUIRE(warned);
    bus.drain();
}

TEST_CASE("a cancelled compile reports Cancelled and never publishes a program") {
    core::JobSystem jobs(2);
    Session session(nullptr, &jobs);
    REQUIRE(session.selectDevice("sc_heavyhex_127").has_value());
    std::string text = "pragma qlab.layout physical\nqubit[40] q;\nbit[40] c;\nh q[0];\n";
    for (int i = 0; i < 39; ++i) text += std::format("cx q[{}], q[{}];\n", i, i + 1);
    text += "c = measure q;\n";
    auto id = session.loadProgram(source(text), "big.qasm");
    REQUIRE(id);
    auto handle = session.compile(*id);
    REQUIRE(handle);
    session.cancelCompile(*handle);
    auto st = session.waitCompile(*handle);
    if (!st) {
        REQUIRE(st.error().code == ErrorCode::Cancelled);
        REQUIRE(session.compiled(*handle) == nullptr);
    } else {
        REQUIRE(session.compiled(*handle) != nullptr); // it beat the cancellation; both are legal
    }
    REQUIRE(session.compileDone(*handle));
}

TEST_CASE("program pragmas override the run options for that program only") {
    Lab& l = lab("sc_fixed_5");
    // `pragma qlab.shots 1024` in bell.qasm applies when the dialog leaves shots at the default.
    const RunResult pragma = l.run(readAsset("Programs/Examples/Basics/bell.qasm"), RunOptions{});
    REQUIRE(pragma.counts.total() == 1024);
    RunOptions explicitShots;
    explicitShots.shots = 256;
    explicitShots.seed = 2;
    REQUIRE(l.run(readAsset("Programs/Examples/Basics/bell.qasm"), explicitShots).counts.total() == 256);
    // `pragma qlab.seed` and `pragma qlab.backend` likewise.
    const RunResult seeded = l.run(source("pragma qlab.seed 4242\npragma qlab.backend statevector\n"
                                          "pragma qlab.shots 128\nqubit[2] q;\nbit[2] c;\nh q[0];\n"
                                          "cx q[0], q[1];\nc = measure q;\n"),
                                   RunOptions{});
    REQUIRE(seeded.seed == 4242);
    REQUIRE(seeded.backend == qsim::Kind::StateVector);   // pinned by the program, not the §2 table
    REQUIRE(seeded.counts.total() == 128);
    // A program that names another device is refused rather than compiled for the wrong one.
    auto id = l.session.loadProgram(source("pragma qlab.device ion_chain_11\nqubit[1] q;\nbit c;\n"
                                           "x q[0];\nc = measure q[0];\n"),
                                    "other.qasm");
    REQUIRE(id);
    auto handle = l.session.compile(*id);
    REQUIRE_FALSE(handle.has_value());
    REQUIRE(handle.error().code == err::NoDevice);
}

TEST_CASE("an unseeded run records the seed it used and repeats it") {
    Lab& l = lab("sc_fixed_5");
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 64;
    const std::string text = source("qubit[2] q;\nbit[2] c;\nh q[0];\ncx q[0], q[1];\nc = measure q;\n");
    const RunResult a = l.run(text, options);
    const RunResult b = l.run(text, options);
    REQUIRE(a.seed != 0);
    REQUIRE(a.seed == b.seed);           // derived from the program hash, never from the wall clock
    REQUIRE(a.options.shots == 64);
    REQUIRE(a.memory == b.memory);
}
