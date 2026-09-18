// Spec 14 §9, 25 §4 — scheduling oracles: durations equal the calibration values on the device
// grid, no two operations overlap on a qubit, every start ≥ every predecessor's end, barriers
// synchronise, ASAP and ALAP share the critical path, idle intervals, QL4010 / QL4080 / QL4090.
//
// The "misplacement" check defined here: a node is misplaced under ASAP when it starts later than
// the latest end of its predecessors (or than 0 without any), and under ALAP when it ends earlier
// than the earliest start of its successors (or than T_circ without any). Both schedules must have
// none; then start_ASAP ≤ start_ALAP for every node and the slack is 0 on a critical path.
#include "CompilerTestUtil.hpp"
#include "Core/Timer.hpp"
#include "Pulse/Schedule.hpp"
#include <algorithm>
#include <iostream>

using namespace ctest;
using compiler::SchedulePolicy;

namespace {
constexpr std::int64_t kGranule = 222 * 16;   // sc_fixed_5: dt = 222 ps, granularity 16 samples

std::int64_t onGrid(double seconds) { return pulse::quantise(seconds, Picoseconds{222}, 16).get(); }

compiler::ScheduleInfo scheduled(ir::Circuit& c, SchedulePolicy policy, const std::string& id = "sc_fixed_5") {
    compiler::ScheduleOptions o;
    o.policy = policy;
    auto info = compiler::schedule(c, device(id).device, device(id).calibration, o);
    INFO((info ? std::string() : info.error().format()));
    REQUIRE(info.has_value());
    return std::move(*info);
}

// Predecessors of every node by wire and by classical bit (spec 14 §3 implied edges).
std::vector<std::vector<std::size_t>> predecessors(const ir::Circuit& c) {
    const auto order = c.topologicalOrder();
    std::vector<std::vector<std::size_t>> preds(order.size());
    std::vector<long> lastWire(c.qubitCount(), -1), lastBit(c.clbitCount(), -1);
    for (std::size_t i = 0; i < order.size(); ++i) {
        const ir::Node& n = c.node(order[i]);
        for (ir::Wire w : ir::nodeWiresIn(n, c.qubitCount())) {
            if (lastWire[w.index] >= 0) preds[i].push_back(static_cast<std::size_t>(lastWire[w.index]));
            lastWire[w.index] = static_cast<long>(i);
        }
        auto bits = ir::nodeWrites(n);
        for (auto b : ir::nodeReads(n)) bits.push_back(b);
        for (auto b : bits) {
            if (lastBit[b.index] >= 0 && lastBit[b.index] != static_cast<long>(i)) preds[i].push_back(static_cast<std::size_t>(lastBit[b.index]));
            lastBit[b.index] = static_cast<long>(i);
        }
    }
    return preds;
}

// Dependencies respected, everything on the grid, and no misplaced node for the policy.
void requireWellPlaced(const ir::Circuit& c, const compiler::ScheduleInfo& s) {
    const auto preds = predecessors(c);
    REQUIRE(s.nodes.size() == preds.size());
    std::vector<std::int64_t> earliestSuccessor(preds.size(), s.duration.get());
    for (std::size_t i = 0; i < preds.size(); ++i) {
        REQUIRE(s.nodes[i].start.get() % kGranule == 0);
        REQUIRE(s.nodes[i].duration.get() % kGranule == 0);
        REQUIRE(s.nodes[i].end() <= s.duration);
        std::int64_t ready = 0;
        for (std::size_t p : preds[i]) {
            REQUIRE(s.nodes[i].start >= s.nodes[p].end());   // no overlap on a qubit, order kept
            ready = std::max(ready, s.nodes[p].end().get());
            earliestSuccessor[p] = std::min(earliestSuccessor[p], s.nodes[i].start.get());
        }
        if (s.policy == SchedulePolicy::Asap) REQUIRE(s.nodes[i].start.get() == ready);
    }
    if (s.policy == SchedulePolicy::Alap)
        for (std::size_t i = 0; i < preds.size(); ++i) REQUIRE(s.nodes[i].end().get() == earliestSuccessor[i]);
}
} // namespace

TEST_CASE("durations are the calibrated values on the device grid; ASAP and ALAP start times by hand") {
    const std::string src = program("pragma qlab.layout physical\nbit[2] c;\nrz(0.3) $0; sx $0; cx $0, $1; x $1; c[0] = measure $0; c[1] = measure $1;");
    const auto& cal = device("sc_fixed_5").calibration;
    const std::int64_t sx = onGrid(cal.qubits[0].duration1q.value.si()), cx = onGrid(cal.edge(0, 1)->duration.value.si()),
                       ro = onGrid(cal.qubits[0].readoutDuration.value.si());
    CHECK(sx == 9 * kGranule);        // 32 ns   → 31.968 ns
    CHECK(cx == 124 * kGranule);      // 440 ns  → 440.448 ns
    CHECK(ro == 197 * kGranule);      // 700 ns  → 699.744 ns

    ir::Circuit asap = build(src);
    const auto a = scheduled(asap, SchedulePolicy::Asap);
    requireWellPlaced(asap, a);
    const std::vector<std::int64_t> lengths{0, sx, cx, sx, ro, ro}, starts{0, 0, sx, sx + cx, sx + cx, sx + cx + sx};
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK(a.nodes[i].duration.get() == lengths[i]);
        CHECK(a.nodes[i].start.get() == starts[i]);
    }
    CHECK(a.duration.get() == 2 * sx + cx + ro);
    CHECK(a.idle.empty());                                   // nothing waits under ASAP here
    // The durations are written on the nodes and the schedule into the metadata.
    CHECK(gatesOf(asap)[2]->duration->get() == cx);
    const auto& meta = asap.meta()["schedule"];
    CHECK(meta["policy"] == "asap");
    CHECK(meta["dt_ps"] == 222);
    CHECK(meta["granule_ps"] == kGranule);
    CHECK(meta["duration_ps"] == a.duration.get());
    CHECK(meta["start_ps"].get<std::vector<std::int64_t>>() == starts);
    CHECK(meta["length_ps"].get<std::vector<std::int64_t>>() == lengths);

    ir::Circuit alap = build(src);
    const auto l = scheduled(alap, SchedulePolicy::Alap);
    requireWellPlaced(alap, l);
    CHECK(l.duration == a.duration);                         // spec 25 §4: the same critical path
    CHECK(l.nodes[4].start.get() == sx + cx + sx);           // the measurement of $0 moves to the end
    REQUIRE(l.idle.contains(0));
    CHECK(l.idle.at(0) == std::vector<compiler::IdleInterval>{{Picoseconds{sx + cx}, Picoseconds{sx + cx + sx}}});
    CHECK(l.totalIdle().get() == sx);
    CHECK(alap.meta()["schedule"]["idle"]["0"][0][1] == sx + cx + sx);
    for (std::size_t i = 0; i < 6; ++i) CHECK(a.nodes[i].start <= l.nodes[i].start);
}

TEST_CASE("random circuits: no misplaced node under either policy, equal critical paths, slack ≥ 0") {
    core::Random rng(17);
    const std::pair<std::uint32_t, std::uint32_t> edges[] = {{0, 1}, {1, 2}, {1, 3}, {3, 4}};
    for (int trial = 0; trial < 20; ++trial) {
        std::vector<ir::Gate> gs;
        for (int k = 0; k < 60; ++k) {
            const auto q = static_cast<std::uint32_t>(rng.uniformInt(5));
            const auto e = edges[rng.uniformInt(4)];
            switch (rng.uniformInt(4)) {
            case 0: gs.push_back(G("sx", {q})); break;
            case 1: gs.push_back(G("rz", {q}, {angle(rng)})); break;
            case 2: gs.push_back(G("x", {q})); break;
            default: gs.push_back(G("cx", {e.first, e.second})); break;
            }
        }
        ir::Circuit a = circuit(5, gs, true), l = a;
        const auto sa = scheduled(a, SchedulePolicy::Asap), sl = scheduled(l, SchedulePolicy::Alap);
        requireWellPlaced(a, sa);
        requireWellPlaced(l, sl);
        REQUIRE(sa.duration == sl.duration);
        std::size_t critical = 0;
        for (std::size_t i = 0; i < sa.nodes.size(); ++i) {
            REQUIRE(sa.nodes[i].start <= sl.nodes[i].start);
            REQUIRE(sa.nodes[i].duration == sl.nodes[i].duration);
            critical += sa.nodes[i].start == sl.nodes[i].start ? 1 : 0;
        }
        CHECK(critical >= 1);   // the critical path has no slack
    }
}

TEST_CASE("barrier, delay, reset, branch and box durations") {
    const auto& dev = device("sc_fixed_5").device;
    const auto& cal = device("sc_fixed_5").calibration;
    const std::int64_t x = onGrid(cal.qubits[0].duration1q.value.si());
    ir::Circuit c = build(program(R"(pragma qlab.layout physical
bit c;
x $0; x $0; barrier $0, $1; x $1;
delay[100ns] $2; delay[10dt] $2; reset $3;
c = measure $0;
if (c == 1) { x $1; x $1; x $1; } else { x $1; }
box[200ns] { x $4; x $4; }
)"));
    const auto s = scheduled(c, SchedulePolicy::Asap);
    requireWellPlaced(c, s);
    CHECK(s.nodes[2].duration.get() == 0);                       // barrier
    CHECK(s.nodes[2].start.get() == 2 * x);                      // … waits for $0
    CHECK(s.nodes[3].start.get() == 2 * x);                      // x $1 starts after the barrier, not at 0
    CHECK(s.nodes[4].duration.get() == 29 * kGranule);           // 100 ns rounded up to the grid: 103.008 ns
    CHECK(s.nodes[5].duration.get() == kGranule);                // 10 dt = 2.22 ns → one granule
    const auto* delay = std::get_if<ir::Delay>(&c.node(c.topologicalOrder()[5]));
    REQUIRE(delay != nullptr);
    CHECK_FALSE(delay->duration.symbolic());                     // dt resolved with the device sample period
    CHECK(delay->duration.ps.get() == kGranule);
    // Active reset = readout + feedback latency + x (spec 09 §2).
    CHECK(s.nodes[6].duration.get() == onGrid(dev.timing.readout.si() + dev.control.feedbackLatency.si() + cal.qubits[3].duration1q.value.si()));
    // Branch = feedback latency + the longer arm; its arms carry their own schedules.
    const std::int64_t latency = onGrid(dev.control.feedbackLatency.si());
    CHECK(s.nodes[8].duration.get() == latency + 3 * x);
    CHECK(s.nodes[8].start >= s.nodes[7].end());                 // after the measurement it reads
    const auto* branch = std::get_if<ir::Branch>(&c.node(c.topologicalOrder()[8]));
    REQUIRE(branch != nullptr);
    CHECK((*branch->thenBody).meta()["schedule"]["duration_ps"] == 3 * x);
    CHECK((*branch->elseBody).meta()["schedule"]["duration_ps"] == x);
    CHECK(s.nodes[9].duration.get() == 57 * kGranule);           // box[200ns] → 202.464 ns ≥ its 2·x body

    ir::Circuit tight = build(program("pragma qlab.layout physical\nbox[40ns] { x $4; x $4; }"));
    auto overrun = compiler::schedule(tight, dev, cal);
    REQUIRE_FALSE(overrun.has_value());
    CHECK(overrun.error().code == compiler::err::BoxOverrun);
    CHECK(tight.nodeCount() == 1);                               // the circuit is left well formed
}

TEST_CASE("QL4090 over the maximum program duration, QL4080 without calibration, QL4010 without a device") {
    const auto& d = device("sc_fixed_5");
    ir::Circuit slow = build(program("pragma qlab.layout physical\nx $0; delay[20ms] $0; x $0;"));   // max_program_duration_ms = 10
    auto tooLong = compiler::schedule(slow, d.device, d.calibration);
    REQUIRE_FALSE(tooLong.has_value());
    CHECK(tooLong.error().diagnosticId == "QL4090");
    CHECK(tooLong.error().message.find("10.000 ms") != std::string::npos);
    compiler::ScheduleOptions relaxed;
    relaxed.enforceMaxDuration = false;
    CHECK(compiler::schedule(slow, d.device, d.calibration, relaxed).has_value());

    ir::Gate stray = G("cx", {0, 2});   // $0–$2 is not an edge: no calibration entry
    stray.span = SourceSpan{3, 1, 3, 10, "s.qasm"};
    ir::Circuit uncalibrated = circuit(5, {stray}, true);
    auto missing = compiler::schedule(uncalibrated, d.device, d.calibration);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().diagnosticId == "QL4080");
    CHECK(missing.error().message == "no calibration for 'cx' on $0, $2");
    CHECK(missing.error().span->line == 3);

    const ir::Circuit symbolic = build(program("qubit q; x q; delay[16dt] q;"));
    auto unresolved = compiler::requireResolvedDurations(symbolic);
    REQUIRE_FALSE(unresolved.has_value());
    CHECK(unresolved.error().diagnosticId == "QL4010");
    CHECK(unresolved.error().span->line == 3);
    CHECK(compiler::requireResolvedDurations(build(program("qubit q; delay[16ns] q;"))).has_value());
}

TEST_CASE("performance: scheduling 10 000 operations (spec 24 §6: 30 ms in Release)") {
    core::Random rng(8);
    std::vector<ir::Gate> gs;
    const std::pair<std::uint32_t, std::uint32_t> edges[] = {{0, 1}, {1, 2}, {1, 3}, {3, 4}};
    for (int k = 0; k < 10000; ++k) {
        const auto e = edges[rng.uniformInt(4)];
        if (k % 3 == 0) gs.push_back(G("cx", {e.first, e.second}));
        else gs.push_back(G(k % 3 == 1 ? "sx" : "rz", {static_cast<std::uint32_t>(rng.uniformInt(5))}, k % 3 == 1 ? std::vector<double>{} : std::vector<double>{0.1}));
    }
    ir::Circuit c = circuit(5, gs, true);
    core::Timer timer;
    const auto s = scheduled(c, SchedulePolicy::Alap);
    const double ms = timer.ms();
    std::cout << "[perf] ALAP schedule of 10000 ops: " << ms << " ms (budget 30 ms in Release)\n";
    CHECK(s.nodes.size() == 10000);
#ifdef NDEBUG
    CHECK(ms < 30.0);
#else
    CHECK(ms < 1500.0);
#endif
}
