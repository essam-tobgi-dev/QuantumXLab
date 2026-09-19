// Spec 14 §1, §11 — the fixed pipeline: pass order per context, per-pass metrics and timing, pragma
// resolution, cancellation, level 2, and diagnostics with spans in the spec's text format.
#include "CompilerTestUtil.hpp"
#include "Core/Timer.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace ctest;
using Names = std::vector<std::string>;

namespace {
compiler::CompileContext contextFor(const std::string& id, compiler::OptimizeLevel level,
                                    bool pulse = false) {
    compiler::CompileContext c;
    if (!id.empty()) {
        c.device = &device(id).device;
        c.calibration = &device(id).calibration;
    }
    c.level = level;
    c.pulseLevel = pulse;
    return c;
}
} // namespace

TEST_CASE("standard pipeline: the fixed pass order of spec 14 §1.1 for each context") {
    using L = compiler::OptimizeLevel;
    CHECK(compiler::PassManager::standard(contextFor("", L::O1)).passNames() ==
          Names{"Verify", "Decompose", "Optimize", "Verify", "Equivalence"});
    CHECK(compiler::PassManager::standard(contextFor("", L::O0)).passNames() ==
          Names{"Verify", "Decompose", "Verify", "Equivalence"});
    CHECK(compiler::PassManager::standard(contextFor("sc_fixed_5", L::O1)).passNames() ==
          Names{"Verify", "Decompose", "Optimize", "Layout", "Route", "Decompose", "Optimize",
                "VirtualZ", "Schedule", "Verify", "Equivalence"});
    CHECK(compiler::PassManager::standard(contextFor("sc_fixed_5", L::O0)).passNames() ==
          Names{"Verify", "Decompose", "Layout", "Route", "Decompose", "Schedule", "Verify",
                "Equivalence"});
    const auto pulsed = compiler::PassManager::standard(contextFor("sc_fixed_5", L::O2, true));
    CHECK(pulsed.passNames()[9] == "PulseLower");
    CHECK(pulsed.frontEnd() == 3); // Layout is the first device-dependent pass
}

TEST_CASE("custom pipelines: user passes implement IPass and appear in the trace") {
    struct DropBarriers final : compiler::IPass {
        std::string_view name() const override { return "DropBarriers"; }
        Result<compiler::PassMetrics> run(ir::Circuit& c, compiler::PassContext& ctx) override {
            std::vector<ir::Node> kept;
            for (ir::Node& n : compiler::takeNodes(c))
                if (!std::holds_alternative<ir::Barrier>(n))
                    kept.push_back(std::move(n));
            compiler::setNodes(c, std::move(kept));
            ctx.diagnostics.push_back(lang::Diagnostics::make(
                "QL4060", SourceSpan{})); // any finding travels with the pass
            return ctx.metrics(c);
        }
    };
    compiler::PassManager pm{compiler::CompileContext{}};
    pm.add(compiler::makeVerifyPass());
    pm.add(std::make_unique<DropBarriers>());
    pm.add(compiler::makeDecomposePass());
    auto out = pm.run(build(program("qubit[2] q; h q[0]; barrier q; cx q[0], q[1];")));
    REQUIRE(out.has_value());
    CHECK(pm.passNames() == Names{"Verify", "DropBarriers", "Decompose"});
    CHECK(out->circuit.nodeCount() == 2); // U + cx, the barrier is gone
    REQUIRE(out->trace.size() == 3);
    CHECK(out->trace[1].pass == "DropBarriers");
    CHECK(out->trace[1].diagnostics.size() == 1);
    CHECK(out->diagnostics.size() == 1);
    CHECK(out->trace[1].before.gateCount == 2);
    CHECK(out->trace[2].after.gateCount == 2);
}

TEST_CASE("per-pass metrics and timing: QFT-4 on sc_heavyhex_27") {
    const auto prog = parse(program(R"(qubit[4] q;
for int i in [3:-1:0] { h q[i]; for int j in [i-1:-1:0] { cp(pi / (1 << (i - j))) q[j], q[i]; } }
swap q[0], q[3]; swap q[1], q[2];
)"));
    const auto& d = device("sc_heavyhex_27");
    auto out = compiler::compile(prog, d.device, d.calibration);
    INFO((out ? std::string() : compiler::formatError(out.error())));
    REQUIRE(out.has_value());
    Names names;
    for (const auto& r : out->trace)
        names.push_back(r.pass);
    CHECK(names == Names{"Build", "Verify", "Decompose", "Optimize", "Layout", "Route", "Decompose",
                         "Optimize", "VirtualZ", "Schedule", "Verify", "Equivalence"});
    const auto& tr = out->trace;
    CHECK(tr[0].after.gateCount == 12); // 4 h + 6 cp + 2 swap
    CHECK(tr[0].after.twoQubitCount == 8);
    CHECK(tr[2].before == tr[1].after); // each pass starts from its predecessor's result
    CHECK(tr[2].after.twoQubitCount == 6 * 2 + 2 * 3); // cp → 2 cx, swap → 3 cx
    CHECK(tr[3].after.gateCount < tr[3].before.gateCount);
    CHECK(tr[5].after.swapCount == out->metrics.swapCount);
    CHECK(tr[5].after.gateCount == tr[5].before.gateCount + tr[5].after.swapCount);
    CHECK(tr[6].after.twoQubitCount ==
          tr[5].after.twoQubitCount + 2 * tr[5].after.swapCount); // each swap → 3 cx
    CHECK(tr[8].after.estimatedDuration.get() == 0); // nothing is timed before Schedule
    CHECK(tr[9].after.estimatedDuration == out->timing.duration);
    CHECK(out->timing.duration.get() > 0);
    CHECK(out->metrics.depth == out->circuit.depth());
    for (const auto& r : tr)
        CHECK(r.wallTime.count() >= 0);
    CHECK(out->deviceId == "sc_heavyhex_27");
    CHECK(out->calibrationTimestamp == d.calibration.timestamp);
    REQUIRE(out->equivalence.has_value());
    CHECK(out->equivalence->equivalent);
    CHECK(out->initialLayout.size() == 4);
    CHECK(out->circuit.layout() == out->initialLayout.v2p);
    // Output contract: native gates on coupled pairs of the device, physical wires.
    const auto target = targetOf("sc_heavyhex_27");
    for (const ir::Gate* g : gatesOf(out->circuit)) {
        CHECK(target.accepts(*g));
        if (g->width() == 2)
            CHECK(d.device.nativeDirection(g->targets[0].index, g->targets[1].index));
    }
}

TEST_CASE("GHZ-20 on sc_heavyhex_127: the line embeds without swaps and the Clifford check covers "
          "all 127 wires") {
    const auto prog = parse(program("qubit[20] q; bit[20] c;\nh q[0];\nfor int i in [0:18] { cx "
                                    "q[i], q[i+1]; }\nc = measure q;\n"));
    const auto& d = device("sc_heavyhex_127");
    auto out = compiler::compile(prog, d.device, d.calibration);
    INFO((out ? std::string() : compiler::formatError(out.error())));
    REQUIRE(out.has_value());
    CHECK(out->metrics.swapCount == 0); // VF2 found an exact embedding of the 20-line
    CHECK(out->circuit.meta()["layout_policy"] == "noise_aware");
    CHECK(out->metrics.twoQubitCount == 19);
    REQUIRE(out->equivalence.has_value());
    CHECK(out->equivalence->method ==
          compiler::EquivalenceMethod::Skipped); // more than ten program qubits: on demand
    // On demand: strip the measurements and compare the Clifford tableaux of source and output.
    auto stripped = [](const ir::Circuit& c) {
        ir::Circuit u = compiler::shellLike(c);
        for (auto id : c.topologicalOrder())
            if (std::holds_alternative<ir::Gate>(c.node(id)))
                u.add(c.node(id));
        return u;
    };
    auto report = compiler::checkEquivalence(stripped(out->source), stripped(out->circuit));
    REQUIRE(report.has_value());
    INFO(report->detail);
    CHECK(report->method == compiler::EquivalenceMethod::Clifford);
    CHECK(report->equivalent);
    CHECK(report->wires == 127);
    // Noise-aware placement: the chosen chain is cheaper than the first 20 qubits in index order.
    const auto g = compiler::CouplingGraph::build(d.device, &d.calibration);
    REQUIRE(g.has_value());
    const auto ig = compiler::interactionGraph(out->source);
    CHECK(compiler::layoutScore(out->initialLayout, ig, *g) <
          compiler::layoutScore(compiler::Layout::identity(20), ig, *g));
}

TEST_CASE("pragmas set the context unless an option overrides them") {
    const auto& d = device("sc_fixed_5");
    const auto prog = parse(
        program("pragma qlab.optimize 0\npragma qlab.layout trivial\npragma qlab.routing "
                "none\npragma qlab.seed 77\nqubit[3] q; h q[0]; cx q[0], q[1]; cx q[1], q[2];"));
    const auto ctx = compiler::resolveContext(prog, {}, &d.device, &d.calibration);
    CHECK(ctx.level == compiler::OptimizeLevel::O0);
    CHECK(ctx.layout == compiler::LayoutPolicy::Trivial);
    CHECK(ctx.routing == compiler::RoutingPolicy::None);
    CHECK(ctx.seed == 77);
    auto out = compiler::compile(prog, d.device, d.calibration);
    REQUIRE(out.has_value());
    CHECK(out->initialLayout.v2p ==
          std::vector<std::uint32_t>{0, 1, 2}); // trivial, 0-1 and 1-2 are edges
    for (const auto& r : out->trace)
        CHECK(r.pass != "Optimize");
    compiler::CompileOptions o;
    o.level = compiler::OptimizeLevel::O1;
    o.routing = compiler::RoutingPolicy::Sabre;
    const auto overridden = compiler::resolveContext(prog, o, &d.device, &d.calibration);
    CHECK(overridden.level == compiler::OptimizeLevel::O1);
    CHECK(overridden.routing == compiler::RoutingPolicy::Sabre);
    // `CompileOptions::inputs` binds `input` declarations (spec 14 §2): ry(0) is the identity.
    const auto sweep =
        parse(program("input float theta = 0.5;\nqubit q; bit c;\nry(theta) q; c = measure q;"));
    compiler::CompileOptions bound;
    bound.inputs = {{"theta", 0.0}};
    auto flat = compiler::compile(sweep, d.device, d.calibration, bound),
         dflt = compiler::compile(sweep, d.device, d.calibration);
    REQUIRE(flat.has_value());
    REQUIRE(dflt.has_value());
    CHECK(flat->metrics.gateCount == 0);
    CHECK(dflt->metrics.gateCount >= 2); // two sx pulses with their frame changes
    // routing none on a circuit that violates the coupling map: QL4030 at the offending gate.
    const auto far = parse(program(
        "pragma qlab.layout trivial\npragma qlab.routing none\nqubit[3] q;\ncx q[0], q[2];"));
    auto violation = compiler::compile(far, d.device, d.calibration);
    REQUIRE_FALSE(violation.has_value());
    CHECK(violation.error().diagnosticId == "QL4030");
    CHECK(violation.error().span->line == 6);
}

TEST_CASE("level 2 keeps the best of four routing seeds; cancellation stops the pipeline") {
    core::Random rng(4);
    std::string body = "qubit[8] q;\n";
    for (int k = 0; k < 60; ++k) {
        const auto a = rng.uniformInt(8);
        auto b = rng.uniformInt(7);
        if (b >= a)
            ++b;
        body += "cx q[" + std::to_string(a) + "], q[" + std::to_string(b) + "];\n";
    }
    const auto prog = parse(program(body));
    const auto& d = device("sc_heavyhex_27");
    compiler::CompileOptions o1, o2;
    o1.level = compiler::OptimizeLevel::O1;
    o2.level = compiler::OptimizeLevel::O2;
    o1.seed = o2.seed = 5;
    o1.verifyEquivalence = o2.verifyEquivalence = false;
    auto a = compiler::compile(prog, d.device, d.calibration, o1),
         b = compiler::compile(prog, d.device, d.calibration, o2);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(b->metrics.swapCount <= a->metrics.swapCount); // trial 0 of level 2 is the level-1 run
    CHECK(a->metrics.swapCount > 0);

    std::stop_source stop;
    stop.request_stop();
    compiler::PassManager pm = compiler::PassManager::standard(
        compiler::resolveContext(prog, {}, &d.device, &d.calibration));
    auto cancelled = pm.run(prog, {}, stop.get_token());
    REQUIRE_FALSE(cancelled.has_value());
    CHECK(cancelled.error().code == ErrorCode::Cancelled);
    REQUIRE(pm.trace().size() == 2); // Build ran, Verify found the stop request
    CHECK(pm.trace()[1].diagnostics.size() == 1);
}

TEST_CASE(
    "compile errors carry the QL4xxx id, the statement span, and print in the spec 14 §11 format") {
    const auto& d = device("sc_fixed_5");
    std::string many = "qubit[10] q;\nh q[0];\nctrl(9) @ x q[0], q[1], q[2], q[3], q[4], q[5], "
                       "q[6], q[7], q[8], q[9];\n";
    auto tooMany = compiler::compileSource(program(many), "big.qasm", nullptr, nullptr);
    REQUIRE_FALSE(tooMany.has_value());
    CHECK(tooMany.error().diagnosticId == "QL4050");
    CHECK(compiler::formatError(tooMany.error()) ==
          "big.qasm:5:1: error[QL4050]: 'x' has 9 controls; the ancilla-free decomposition is "
          "limited to 8\n"
          "  help: use ancilla qubits and ccx ladders");
    auto dt = compiler::compileSource(program("qubit q;\nx q;\ndelay[32dt] q;\n"), "dt.qasm",
                                      nullptr, nullptr);
    REQUIRE_FALSE(dt.has_value());
    CHECK(compiler::formatError(dt.error()) ==
          "dt.qasm:5:1: error[QL4010]: 'dt' duration used but no device is selected\n"
          "  help: select a device ('pragma qlab.device <id>') or write the duration in ns, us or "
          "ms");
    CHECK(compiler::compileSource(program("qubit q;\nx q;\ndelay[32dt] q;\n"), "dt.qasm", &d.device,
                                  &d.calibration)
              .has_value());
    // A front-end error comes back unchanged.
    auto syntax =
        compiler::compileSource(program("qubit q;\nfoo q;\n"), "bad.qasm", nullptr, nullptr);
    REQUIRE_FALSE(syntax.has_value());
    CHECK(syntax.error().diagnosticId.starts_with("QL3"));
    // Info diagnostics do not stop the compile: QL4060 from a starved VF2 search.
    compiler::CompileOptions o;
    o.layout = compiler::LayoutPolicy::Vf2;
    o.vf2StateBudget = 3;
    auto ring = compiler::compileSource(
        program("qubit[12] q;\nfor int i in [0:10] { cz q[i], q[i+1]; }\ncz q[11], q[0];\n"),
        "ring.qasm", &device("sc_heavyhex_27").device, &device("sc_heavyhex_27").calibration, o);
    REQUIRE(ring.has_value());
    REQUIRE(ring->diagnostics.size() == 1);
    CHECK(compiler::formatDiagnostic(ring->diagnostics[0]) ==
          "info[QL4060]: VF2 layout search exceeded its budget; falling back to the dense layout");
    CHECK(ring->programHash ==
          compiler::programHash(program(
              "qubit[12] q;\nfor int i in [0:10] { cz q[i], q[i+1]; }\ncz q[11], q[0];\n")));
}

TEST_CASE("performance: parse + compile of a 1000-gate program (spec 24 §6: 50 ms in Release)") {
    core::Random rng(12);
    std::string body = "qubit[10] q;\nbit[10] c;\n";
    const char* one[] = {"h", "t", "s", "x", "sx"};
    for (int k = 0; k < 1000; ++k) {
        const auto a = rng.uniformInt(10);
        auto b = rng.uniformInt(9);
        if (b >= a)
            ++b;
        if (k % 3 == 0)
            body += "cx q[" + std::to_string(a) + "], q[" + std::to_string(b) + "];\n";
        else if (k % 3 == 1)
            body += "rz(" + std::to_string(rng.uniform(-3.0, 3.0)) + ") q[" + std::to_string(a) +
                    "];\n";
        else
            body += std::string(one[rng.uniformInt(5)]) + " q[" + std::to_string(a) + "];\n";
    }
    body += "c = measure q;\n";
    const std::string text = program(body);
    const auto& d = device("sc_heavyhex_27");
    compiler::CompileOptions o;
    o.verifyEquivalence =
        false; // the budget is for compilation; the checker is a verification tool
    core::Timer timer;
    auto out = compiler::compileSource(text, "k.qasm", &d.device, &d.calibration, o);
    const double ms = timer.ms();
    INFO((out ? std::string() : compiler::formatError(out.error())));
    REQUIRE(out.has_value());
    std::cout << "[perf] parse + compile, 1000 gates on sc_heavyhex_27: " << ms
              << " ms (budget 50 ms in Release); passes:";
    for (const auto& r : out->trace)
        std::cout << ' ' << r.pass << '=' << static_cast<double>(r.wallTime.count()) / 1000.0;
    std::cout << " ms\n";
    // QXL_PERF_SLACK relaxes the budget on shared CI runners (the figure is for a workstation).
    const char* slackEnv = std::getenv("QXL_PERF_SLACK");
    const double slack = slackEnv != nullptr ? std::max(1.0, std::atof(slackEnv)) : 1.0;
#ifdef NDEBUG
    CHECK(ms < 50.0 * slack);
#else
    CHECK(ms < 3000.0 * slack); // loose bound for the unoptimised Debug build
#endif
}
