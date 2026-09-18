// Spec 14 §11 — incremental compilation: 150 ms debounce, a new edit cancels the job in flight,
// passes 1–4 cached by program hash and the whole result by (hash, device, options).
#include "CompilerTestUtil.hpp"
#include "Core/JobSystem.hpp"
#include "Core/Timer.hpp"
#include <atomic>

using namespace ctest;

TEST_CASE("debounce: a burst of edits compiles once, 150 ms after the last one") {
    core::JobSystem jobs(2);
    compiler::IncrementalCompiler inc(jobs);
    const auto& d = device("sc_fixed_5");
    inc.setTarget(&d.device, &d.calibration);
    std::atomic<int> published{0};
    inc.onUpdate([&](const compiler::IncrementalCompiler::Update&) { ++published; });

    core::Timer timer;
    std::uint64_t last = 0;
    for (int edit = 0; edit < 5; ++edit)
        last = inc.submit(program("qubit[2] q; bit[2] c; h q[0]; cx q[0], q[1]; rz(0." + std::to_string(edit) + ") q[1]; c = measure q;"), "edit.qasm");
    const auto update = inc.wait();
    const double ms = timer.ms();
    REQUIRE(update != nullptr);
    CHECK(last == 5);
    CHECK(update->generation == 5);                 // only the newest edit was compiled
    CHECK(published.load() == 1);
    CHECK(inc.cancelledCount() == 4);
    CHECK(ms >= 150.0);                             // the debounce interval elapsed after the last edit
    REQUIRE(update->program.has_value());
    CHECK(update->program->circuit.isPhysical());
    CHECK_FALSE(update->frontEndCached);
    CHECK(update->trace.front().pass == "Build");
    CHECK(update->trace.back().pass == "Equivalence");
    CHECK(update->hash == update->program->programHash);
}

TEST_CASE("caches: identical text is served whole; a device-side option reuses passes 1-4") {
    core::JobSystem jobs(2);
    compiler::IncrementalCompiler::Settings fast;
    fast.debounce = std::chrono::milliseconds(5);
    compiler::IncrementalCompiler inc(jobs, fast);
    const auto& d = device("sc_heavyhex_27");
    inc.setTarget(&d.device, &d.calibration);
    const std::string text = program("qubit[4] q; h q[0]; cx q[0], q[1]; cx q[1], q[2]; cx q[2], q[3]; t q[3];");
    inc.submit(text, "a.qasm");
    const auto first = inc.wait();
    REQUIRE(first != nullptr);
    REQUIRE(first->program.has_value());
    CHECK_FALSE(first->fullyCached);

    inc.submit(text, "a.qasm");
    const auto second = inc.wait();
    REQUIRE(second->program.has_value());
    CHECK(second->fullyCached);
    CHECK(second->generation == 2);
    CHECK(second->program->circuit.structurallyEqual(first->program->circuit));

    compiler::CompileOptions trivial;
    trivial.layout = compiler::LayoutPolicy::Trivial;
    inc.setTarget(&d.device, &d.calibration, trivial);
    inc.submit(text, "a.qasm");
    const auto third = inc.wait();
    REQUIRE(third->program.has_value());
    CHECK(third->frontEndCached);
    CHECK_FALSE(third->fullyCached);
    CHECK(third->program->initialLayout.v2p == std::vector<std::uint32_t>{0, 1, 2, 3});
    CHECK(third->trace.front().pass == "Build");    // the cached front trace is part of the published trace
    REQUIRE(third->program->equivalence.has_value());
    CHECK(third->program->equivalence->equivalent);

    // An edit changes the hash: nothing is reused.
    inc.submit(text + "x q[0];\n", "a.qasm");
    const auto fourth = inc.wait();
    CHECK_FALSE(fourth->frontEndCached);
    CHECK(fourth->hash != third->hash);
}

TEST_CASE("errors are published with their diagnostics; cancel drops the job in flight") {
    core::JobSystem jobs(1);
    compiler::IncrementalCompiler::Settings fast;
    fast.debounce = std::chrono::milliseconds(5);
    compiler::IncrementalCompiler inc(jobs, fast);
    inc.submit(program("qubit q;\nfoo q;\n"), "bad.qasm");        // device-independent target
    const auto bad = inc.wait();
    REQUIRE(bad != nullptr);
    REQUIRE_FALSE(bad->program.has_value());
    CHECK(bad->program.error().diagnosticId.starts_with("QL3"));
    CHECK(lang::hasErrors(bad->diagnostics));

    inc.submit(program("qubit[9] q;\nctrl(9) @ x q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[8];\n"), "bad2.qasm");
    const auto sema = inc.wait();
    REQUIRE_FALSE(sema->program.has_value());

    inc.submit(program("qubit q;\nx q;\ndelay[4dt] q;\n"), "dt.qasm");
    const auto dt = inc.wait();
    REQUIRE_FALSE(dt->program.has_value());
    CHECK(dt->program.error().diagnosticId == "QL4010");
    CHECK(dt->trace.back().pass == "Verify");                      // the trace ends at the pass that failed
    CHECK(dt->trace.back().diagnostics.size() == 1);

    const auto before = inc.latest();
    compiler::IncrementalCompiler::Settings slow;
    slow.debounce = std::chrono::milliseconds(400);
    compiler::IncrementalCompiler cancellable(jobs, slow);
    cancellable.submit(program("qubit q; x q;"), "c.qasm");
    cancellable.cancel();
    CHECK(cancellable.wait() == nullptr);                          // nothing was ever published
    CHECK(cancellable.cancelledCount() == 1);
    CHECK(inc.latest() == before);
}
