// Spec 14 §10, 25 §4/§6 — the full pipeline on every shipped example program: it compiles for every
// device it fits, the output is native and respects the coupling map, and for programs of at most
// ten qubits the compiled circuit is equivalent to the source (checked again here, on demand, with
// an unlimited work budget). Also the compiled-program export round trip of spec 14 §11.
#include "CompilerTestUtil.hpp"
#include <algorithm>
#include <filesystem>
#include <format>
#include <iostream>

using namespace ctest;
namespace fs = std::filesystem;

namespace {
std::vector<fs::path> examplePrograms() {
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(core::assetDir() / "Programs" / "Examples"))
        if (e.is_regular_file() && e.path().extension() == ".qasm") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    return files;
}

void requireNativeOnDevice(const compiler::CompiledProgram& out, const hw::LoadedDevice& d, const compiler::Target& target) {
    REQUIRE(out.circuit.isPhysical());
    REQUIRE(out.circuit.qubitCount() == d.device.qubitCount());
    REQUIRE(ir::verify(out.circuit).has_value());
    std::vector<const ir::Circuit*> levels{&out.circuit};
    while (!levels.empty()) {
        const ir::Circuit* c = levels.back();
        levels.pop_back();
        for (auto id : c->topologicalOrder()) {
            const ir::Node& n = c->node(id);
            compiler::forEachBody(n, [&](const ir::Circuit& body) { levels.push_back(&body); });
            const auto* g = std::get_if<ir::Gate>(&n);
            if (!g) continue;
            INFO("gate " << g->name);
            REQUIRE(target.accepts(*g));
            for (ir::Wire w : g->wires()) REQUIRE_FALSE(d.device.isCoupler(w.index));
            if (g->width() == 2 && !g->opaque) REQUIRE(d.device.nativeDirection(g->targets[0].index, g->targets[1].index));
            REQUIRE(g->duration.has_value());
        }
    }
}
} // namespace

TEST_CASE("every example program compiles for every device it fits and stays equivalent to its source") {
    const auto files = examplePrograms();
    REQUIRE(files.size() == 30);
    std::size_t compiled = 0, checked = 0;
    for (const auto& file : files) {
        auto text = core::readTextFile(file);
        REQUIRE(text.has_value());
        const lang::Program prog = parse(*text);
        const bool pulseProgram = file.parent_path().filename() == "Pulse";   // physical qubits + defcals of sc_fixed_5
        std::vector<std::string> devices = pulseProgram ? std::vector<std::string>{"sc_fixed_5"}
                                                        : std::vector<std::string>{"", "sc_fixed_5", "sc_heavyhex_27", "sc_tunable_grid_54", "ion_chain_11"};
        for (const std::string& id : devices) {
            const std::uint32_t capacity = id.empty() ? 1000 : static_cast<std::uint32_t>(device(id).device.dataQubitCount());
            if (prog.qubitCount > capacity) continue;
            INFO(file.filename().string() << " on " << (id.empty() ? "{U, cx}" : id));
            auto out = id.empty() ? compiler::compile(prog) : compiler::compile(prog, device(id).device, device(id).calibration);
            INFO((out ? std::string() : compiler::formatError(out.error())));
            REQUIRE(out.has_value());
            ++compiled;
            if (!id.empty()) {
                requireNativeOnDevice(*out, device(id), targetOf(id));
                CHECK(out->timing.nodes.size() == out->circuit.nodeCount());
                CHECK(out->metrics.estimatedDuration == out->timing.duration);
            } else {
                for (const ir::Gate* g : gatesOf(out->circuit)) CHECK((g->name == "U" || g->name == "cx"));
            }
            if (prog.qubitCount > 10 || pulseProgram) continue;
            // On demand, without the pipeline's work limit; fewer random states keep Debug runs short.
            compiler::EquivalenceOptions eo;
            eo.states = 3;
            eo.branches = 2;
            eo.workLimit = 400'000'000;
            auto report = compiler::checkEquivalence(out->source, out->circuit, eo);
            REQUIRE(report.has_value());
            INFO(compiler::equivalenceMethodName(report->method) << " on " << report->wires << " wires: " << report->detail);
            REQUIRE(report->method != compiler::EquivalenceMethod::Skipped);
            REQUIRE(report->equivalent);
            ++checked;
        }
    }
    std::cout << "[examples] " << compiled << " compiles, " << checked << " equivalence checks\n";
    CHECK(compiled >= 100);
    CHECK(checked >= 80);
}

TEST_CASE("every pipeline setting keeps the examples equivalent; equal seeds give equal output (spec 25 §4)") {
    using compiler::LayoutPolicy;
    using compiler::OptimizeLevel;
    struct Setting { OptimizeLevel level; LayoutPolicy layout; compiler::SchedulePolicy schedule; bool kak; std::string twoQubitBasis; };
    const std::vector<Setting> settings = {
        {OptimizeLevel::O0, LayoutPolicy::Trivial, compiler::SchedulePolicy::Asap, false, ""},
        {OptimizeLevel::O1, LayoutPolicy::Dense, compiler::SchedulePolicy::Alap, false, "ecr"},
        {OptimizeLevel::O1, LayoutPolicy::Vf2, compiler::SchedulePolicy::Asap, true, ""},
        {OptimizeLevel::O2, LayoutPolicy::NoiseAware, compiler::SchedulePolicy::Alap, false, ""},
    };
    const auto& d = device("sc_heavyhex_27");
    std::size_t checked = 0;
    for (const auto& file : examplePrograms()) {
        const std::string group = file.parent_path().filename().string();
        if (group != "Basics" && group != "Fourier" && group != "Protocols" && group != "Variational") continue;
        const lang::Program prog = parse(*core::readTextFile(file));
        for (const Setting& s : settings) {
            compiler::CompileOptions o;
            o.level = s.level;
            o.layout = s.layout;
            o.schedule = s.schedule;
            o.kak = s.kak;
            o.twoQubitBasis = s.twoQubitBasis;
            o.seed = 99;
            auto out = compiler::compile(prog, d.device, d.calibration, o);
            INFO(file.filename().string() << " level " << static_cast<int>(s.level) << " layout " << compiler::layoutPolicyName(s.layout)
                                          << (out ? std::string() : " " + compiler::formatError(out.error())));
            REQUIRE(out.has_value());
            requireNativeOnDevice(*out, d, targetOf("sc_heavyhex_27", s.twoQubitBasis));
            if (!s.twoQubitBasis.empty()) CHECK(countGates(out->circuit, "cx") == 0);   // the chosen entangler only
            compiler::EquivalenceOptions eo;
            eo.states = 3;
            auto report = compiler::checkEquivalence(out->source, out->circuit, eo);
            REQUIRE(report.has_value());
            INFO(report->detail);
            REQUIRE(report->equivalent);
            ++checked;
            // Determinism (DEVELOPMENT.md): the same seed gives the same circuit, layout and timing.
            auto again = compiler::compile(prog, d.device, d.calibration, o);
            REQUIRE(again.has_value());
            CHECK(again->circuit.structurallyEqual(out->circuit, 0.0));
            CHECK(again->initialLayout == out->initialLayout);
            CHECK(again->timing.duration == out->timing.duration);
        }
    }
    CHECK(checked >= 40);
}

TEST_CASE("export: compiled OpenQASM 3 re-imports and compiles to an equivalent circuit with the same timing") {
    const auto& d = device("sc_heavyhex_27");
    const auto prog = parse(program(R"(qubit[3] q; bit[3] c;
h q[0]; cx q[0], q[1]; t q[2]; cx q[0], q[2]; cx q[1], q[2]; delay[50ns] q[1]; ry(0.4) q[1];
c = measure q;
)"));
    compiler::CompileOptions o;
    o.schedule = compiler::SchedulePolicy::Alap;
    auto first = compiler::compile(prog, d.device, d.calibration, o);
    REQUIRE(first.has_value());
    auto text = compiler::exportQasm(*first);
    INFO((text ? *text : text.error().format()));
    REQUIRE(text.has_value());
    CHECK(text->starts_with("OPENQASM 3.0;\n// compiled by quantumxlab "));
    CHECK(text->find(" for sc_heavyhex_27 (calibration " + d.calibration.timestamp + ")") != std::string::npos);
    CHECK(text->find("pragma qlab.layout physical\n") != std::string::npos);
    CHECK(text->find(std::format("pragma qlab.mapping q[0]->${}, q[1]->${}, q[2]->${}\n", first->initialLayout.v2p[0], first->initialLayout.v2p[1],
                                 first->initialLayout.v2p[2])) != std::string::npos);
    CHECK(text->find("pragma qlab.schedule alap dt=0.222ns\n") != std::string::npos);
    CHECK(text->find("// metrics: gates=") != std::string::npos);
    CHECK(text->find("delay[") != std::string::npos);        // idle gaps are explicit
    CHECK(text->find("q[") == text->find("q[0]->$"));        // operands are physical: `q[` appears in the mapping only

    auto again = compiler::compileSource(*text, "exported.qasm", &d.device, &d.calibration);
    INFO((again ? std::string() : compiler::formatError(again.error())));
    REQUIRE(again.has_value());
    CHECK(again->circuit.isPhysical());
    CHECK(again->metrics.swapCount == 0);                    // already on the coupling map
    // Explicit delays make the ASAP reschedule reproduce the ALAP timing of the first compile.
    CHECK(again->timing.duration == first->timing.duration);
    // Round trip: the re-compiled circuit implements the ORIGINAL source under the original mapping.
    ir::Circuit mapped = again->circuit;
    mapped.setLayout(first->initialLayout.v2p);
    mapped.meta()["final_layout"] = first->finalLayout.v2p;
    auto report = compiler::checkEquivalence(first->source, mapped);
    REQUIRE(report.has_value());
    INFO(report->detail);
    CHECK(report->equivalent);

    // Device-independent output exports too (no mapping, no schedule).
    auto plain = compiler::compile(prog);
    REQUIRE(plain.has_value());
    auto plainText = compiler::exportQasm(*plain);
    REQUIRE(plainText.has_value());
    CHECK(plainText->find("qlab.mapping") == std::string::npos);
    CHECK(plainText->find("for {U, cx}") != std::string::npos);
    CHECK(compiler::compileSource(*plainText, "plain.qasm", nullptr, nullptr).has_value());
}
