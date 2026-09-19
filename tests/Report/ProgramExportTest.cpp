// Spec 23 §5 — program export: the compiled OpenQASM 3 with its pragma block re-parses through the
// front end, re-compiles to an equivalent circuit, and keeps its mapping.
#include "ReportTestUtil.hpp"

#include "Compiler/Compiler.hpp"
#include "Hardware/Hardware.hpp"
#include "Lang/Sema.hpp"

using namespace qlab;
using namespace qlab::report;

TEST_CASE("program export: the compiled QASM carries mapping, schedule and estimate pragmas") {
    const rtest::Fixture& f = rtest::bell();
    REQUIRE(f.compiled != nullptr);

    ProgramExportOptions options;
    options.estimate = &f.result.estimate;
    auto text = compiledQasm(*f.compiled, options);
    INFO((text ? *text : text.error().format()));
    REQUIRE(text.has_value());

    CHECK(text->starts_with("OPENQASM 3.0;\n// compiled by quantumxlab "));
    CHECK(text->find(" for sc_fixed_5 (calibration ") != std::string::npos);
    CHECK(text->find("pragma qlab.layout physical\n") != std::string::npos);
    const std::string mapping =
        std::format("pragma qlab.mapping q[0]->${}, q[1]->${}\n", f.compiled->initialLayout.v2p[0],
                    f.compiled->initialLayout.v2p[1]);
    CHECK(text->find(mapping) != std::string::npos);
    CHECK(text->find("pragma qlab.schedule ") != std::string::npos);
    CHECK(text->find("pragma qlab.estimate wall_s=") != std::string::npos);
    CHECK(text->find("fidelity=") != std::string::npos);

    // The estimate pragma joins the pragma block: it is the last `pragma qlab.` line, and the block
    // from the first pragma to it holds nothing but pragma lines.
    const std::size_t estimateAt = text->find("pragma qlab.estimate");
    CHECK(text->rfind("pragma qlab.", std::string::npos) == estimateAt);
    const std::size_t blockStart = text->find("pragma qlab.");
    const std::size_t blockEnd = text->find('\n', estimateAt) + 1;
    REQUIRE(blockStart < blockEnd);
    for (std::size_t at = blockStart; at < blockEnd; at = text->find('\n', at) + 1) {
        INFO(text->substr(at, text->find('\n', at) - at));
        CHECK(text->compare(at, 7, "pragma ") == 0);
    }
    CHECK(estimatePragma(f.result.estimate).find("wall_s=") != std::string::npos);

    // Every pragma is accepted by the front end (spec 13 §7) — the export re-parses cleanly.
    auto parsed = lang::parseProgram(*text, "exported.qasm");
    INFO((parsed ? std::string() : parsed.error().format()));
    REQUIRE(parsed.has_value());
    for (const auto& d : parsed->diagnostics) {
        INFO(compiler::formatDiagnostic(d));
        CHECK_FALSE(d.isError());
    }
}

TEST_CASE(
    "program export: the compiled QASM re-compiles to an equivalent circuit (spec 23 §5, §12)") {
    auto loaded = hw::loadShippedDevice("sc_fixed_5");
    REQUIRE(loaded.has_value());
    const auto source =
        std::string("OPENQASM 3.0;\ninclude \"stdgates.inc\";\n"
                    "qubit[3] q; bit[3] c;\nh q[0];\ncx q[0], q[1];\nt q[2];\ncx q[1], q[2];\n"
                    "ry(0.4) q[1];\nc = measure q;\n");
    auto program = lang::parseProgram(source, "source.qasm");
    REQUIRE(program.has_value());
    compiler::CompileOptions options;
    options.schedule = compiler::SchedulePolicy::Alap;
    auto first = compiler::compile(*program, loaded->device, loaded->calibration, options);
    INFO((first ? std::string() : compiler::formatError(first.error())));
    REQUIRE(first.has_value());

    auto text = compiledQasm(*first);
    REQUIRE(text.has_value());
    auto again =
        compiler::compileSource(*text, "exported.qasm", &loaded->device, &loaded->calibration);
    INFO((again ? std::string() : compiler::formatError(again.error())));
    REQUIRE(again.has_value());
    CHECK(again->circuit.isPhysical());
    CHECK(again->metrics.swapCount == 0); // already routed
    CHECK(again->timing.duration ==
          first->timing.duration); // explicit delays reproduce the schedule

    // The mapping pragma round-trips: the physical qubits it names are the ones the re-imported
    // program acts on, and the re-compiled circuit implements the original source under it.
    for (std::uint32_t v = 0; v < first->initialLayout.size(); ++v) {
        const std::string operand = std::format("q[{}]->${}", v, first->initialLayout.v2p[v]);
        INFO(operand);
        CHECK(text->find(operand) != std::string::npos);
        CHECK(text->find(std::format("${}", first->initialLayout.v2p[v])) != std::string::npos);
    }
    ir::Circuit mapped = again->circuit;
    mapped.setLayout(first->initialLayout.v2p);
    mapped.meta()["final_layout"] = first->finalLayout.v2p;
    auto equivalence = compiler::checkEquivalence(first->source, mapped);
    REQUIRE(equivalence.has_value());
    INFO(equivalence->detail);
    CHECK(equivalence->equivalent);

    // Source export (§5): the editor text, unchanged.
    rtest::Sandbox box("program_export");
    REQUIRE(writeProgramSource(box / "source.qasm", source).has_value());
    CHECK(rtest::readFile(box / "source.qasm") == source);
    REQUIRE(writeCompiledQasm(box / "compiled.qasm", *first).has_value());
    CHECK(rtest::readFile(box / "compiled.qasm") == *text);
}

TEST_CASE("program export: a device-independent compile exports without mapping or schedule") {
    auto program = lang::parseProgram(
        "OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit[2] q;\nh q[0];\ncx q[0], q[1];\n",
        "plain.qasm");
    REQUIRE(program.has_value());
    auto plain = compiler::compile(*program);
    REQUIRE(plain.has_value());
    auto text = compiledQasm(*plain);
    REQUIRE(text.has_value());
    CHECK(text->find("qlab.mapping") == std::string::npos);
    CHECK(text->find("for {U, cx}") != std::string::npos);

    // With an estimate but no pragma block, the estimate line still lands after the include.
    runtime::Estimate estimate;
    estimate.wallTime.valueS = 4.21;
    estimate.fidelity.fast = 0.91;
    ProgramExportOptions options;
    options.estimate = &estimate;
    auto withEstimate = compiledQasm(*plain, options);
    REQUIRE(withEstimate.has_value());
    CHECK(withEstimate->find("pragma qlab.estimate wall_s=4.21 fidelity=0.91\n") !=
          std::string::npos);
    const std::size_t include = withEstimate->find("include \"stdgates.inc\";");
    CHECK(include < withEstimate->find("pragma qlab.estimate"));
    CHECK(compiler::compileSource(*withEstimate, "plain.qasm", nullptr, nullptr).has_value());
}

TEST_CASE("program export: the pulse schedule is sampled per channel on the dt grid (spec 23 §5)") {
    const Picoseconds dt{1000}; // 1 ns
    pulse::Schedule schedule(dt);
    const pulse::ChannelId d0 = pulse::ChannelId::drive(0);
    const pulse::ChannelId d1 = pulse::ChannelId::drive(1);
    schedule.insert(pulse::Play{d0, pulse::Waveform::constant(4e-9, 0.5, 0.0), {}}, Picoseconds{0});
    schedule.insert(pulse::Play{d1, pulse::Waveform::constant(2e-9, 0.25, 0.0), {}},
                    Picoseconds{2000});

    const std::string csv = scheduleSamplesCsv(schedule);
    INFO(csv);
    CHECK(csv.starts_with("t (s),d[0]_re (arb),d[0]_im (arb),d[1]_re (arb),d[1]_im (arb)\n"));
    // 4 ns of samples plus the end point, on a 1 ns grid.
    const std::size_t rows = static_cast<std::size_t>(std::count(csv.begin(), csv.end(), '\n')) - 1;
    CHECK(rows == 5u);
    CHECK(csv.find("\n0,0.5,0,0,0\n") != std::string::npos);        // only d[0] plays at t = 0
    CHECK(csv.find("\n2e-09,0.5,0,0.25,0\n") != std::string::npos); // both play at t = 2 ns
    CHECK(csv.find("\n4e-09,0,0,0,0\n") != std::string::npos);      // both finished at t = 4 ns

    rtest::Sandbox box("schedule_csv");
    REQUIRE(writeScheduleSamplesCsv(box / "channels.csv", schedule).has_value());
    CHECK(rtest::readFile(box / "channels.csv") == csv);
}
