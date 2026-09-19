// Spec 03 §3, 15 §4/§9, 23 §6 — the command line and the headless run. `runHeadless` is what the
// executable calls, so these assertions are about the shipped behaviour and not a test-only path.
//
// Oracle: `bell.qasm` on `sc_fixed_5` with the calibrated noise model must put ≥ 90 % of the shots
// on 00 and 11, balanced within five sigmas, and the two error outcomes must be consistent with
// the calibration's readout and two-qubit error. The `--json` document must be a valid `run_result`
// envelope carrying the Estimate of spec 15 §9.
#include "App/App.hpp"
#include "Core/Paths.hpp"
#include "Core/Version.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <sstream>

using namespace qlab;
using Catch::Approx;

namespace {

std::filesystem::path bellPath() {
    return core::assetDir() / "Programs" / "Examples" / "Basics" / "bell.qasm";
}

app::Options parse(std::vector<std::string_view> args) {
    auto o = app::parseOptions(std::span<const std::string_view>(args));
    if (!o)
        FAIL(o.error().message);
    return *o;
}

} // namespace

TEST_CASE("the command line of spec 03 §3 parses") {
    const app::Options gui = parse({});
    CHECK(gui.mode == app::Mode::Gui);
    CHECK(gui.device == "sc_fixed_5");
    CHECK(gui.layout == "sc_lab_standard");
    CHECK_FALSE(gui.shotsGiven);

    const app::Options project = parse({"my.qxlab"});
    CHECK(project.mode == app::Mode::Gui);
    CHECK(project.project == std::filesystem::path("my.qxlab"));

    const app::Options run =
        parse({"--run", "p.qasm", "--device", "sc_heavyhex_27", "--shots", "4096", "--seed", "7",
               "--backend", "statevector", "--json", "-O2"});
    CHECK(run.mode == app::Mode::Run);
    CHECK(run.program == std::filesystem::path("p.qasm"));
    CHECK(run.device == "sc_heavyhex_27");
    CHECK(run.shots == 4096);
    CHECK(run.shotsGiven);
    CHECK(run.seed == 7);
    CHECK(run.backend == runtime::BackendChoice::StateVector);
    CHECK(run.json);
    CHECK(run.optimize == 2);
    CHECK(run.layout == "sc_lab_standard");

    // An ion device selects the ion laboratory layout (spec 17 §3.6).
    CHECK(parse({"--device", "ion_chain_11"}).layout == "ion_lab_11");
    // `--flag=value` is the same as `--flag value`.
    CHECK(parse({"--shots=256"}).shots == 256);
    CHECK(parse({"--selftest", "out"}).mode == app::Mode::SelfTest);
    CHECK(parse({"--version"}).mode == app::Mode::Version);
    CHECK(parse({"--help"}).mode == app::Mode::Help);

    // Every malformed argument is an error naming itself, never a silent default.
    for (std::vector<std::string_view> bad : {std::vector<std::string_view>{"--shots"},
                                              {"--shots", "many"},
                                              {"--shots", "0"},
                                              {"--backend", "quantum"},
                                              {"--nonsense"},
                                              {"--run"},
                                              {"a.qxlab", "b.qxlab"}}) {
        const auto r = app::parseOptions(std::span<const std::string_view>(bad));
        CHECK_FALSE(r.has_value());
    }
    CHECK(app::usageText().find("--selftest") != std::string::npos);
    CHECK(app::versionText().find(std::string(core::version())) != std::string::npos);
}

// A Catch2 test name must not start with `--`: `catch_discover_tests` hands it to the binary as the
// first argument, where a leading double dash is an option token, so CTest can never run the case
// ("Unrecognised token: --run") however green the unfiltered binary looks.
TEST_CASE("headless --run of bell.qasm on sc_fixed_5 reproduces the Bell distribution") {
    app::Options o =
        parse({"--run", "x", "--device", "sc_fixed_5", "--shots", "4096", "--seed", "1"});
    o.program = bellPath();
    std::ostringstream out, err;
    REQUIRE(app::runHeadless(o, out, err) == 0);
    const std::string text = out.str();
    // The human-readable report names the device, the backend and the estimate.
    CHECK(text.find("sc_fixed_5") != std::string::npos);
    CHECK(text.find("counts") != std::string::npos);
    CHECK(text.find("wall time") != std::string::npos);
    CHECK(text.find("fidelity") != std::string::npos);

    // Same run, as the `run_result` document of spec 23 §6.
    o.json = true;
    std::ostringstream json;
    REQUIRE(app::runHeadless(o, json, err) == 0);
    core::Json document = core::Json::parse(json.str());
    REQUIRE(document.contains("qxl"));
    CHECK(document["qxl"]["kind"] == "run_result");
    CHECK(document["qxl"]["schema"] == report::kRunResultSchema);
    const core::Json& data = document["data"];
    CHECK(data["device"] == "sc_fixed_5");
    CHECK(data["shots"] == 4096);
    CHECK(data["seed"] == 1);
    CHECK(data["partial"] == false);
    CHECK(data["identity"]["program_hash"].get<std::string>().starts_with("fnv1a64:"));

    const core::Json& counts = data["counts"]["counts"];
    const double total = data["counts"]["total"].get<double>();
    CHECK(total == 4096);
    const double p00 = counts.value("00", 0.0) / total;
    const double p11 = counts.value("11", 0.0) / total;
    const double p01 = counts.value("01", 0.0) / total;
    const double p10 = counts.value("10", 0.0) / total;
    // Spec 15 §4: a Bell pair through a calibrated sc_fixed_5 keeps almost all of its weight on the
    // correlated outcomes, split evenly; the residue is readout and two-qubit error.
    CHECK(p00 + p11 > 0.90);
    CHECK(std::abs(p00 - p11) < 5.0 * std::sqrt(0.25 / total) + 0.02);
    CHECK(p01 + p10 < 0.10);
    CHECK(p01 > 0.0);
    CHECK(p10 > 0.0);

    // Spec 15 §9: the estimate is a complete Model record, and every number in the document is
    // finite.
    const core::Json& estimate = data["estimate"];
    for (const char* key :
         {"wall_time", "fidelity", "resources", "classical_cost", "assumptions", "class"})
        CHECK(estimate.contains(key));
    CHECK(estimate["class"] == "Model");
    CHECK(estimate["wall_time"]["value_s"].get<double>() > 0.0);
    CHECK(estimate["fidelity"]["fast"].get<double>() > 0.5);
    CHECK(estimate["fidelity"]["fast"].get<double>() <= 1.0);
    CHECK(estimate["resources"]["qubits"] == 2);
    CHECK(estimate["assumptions"].size() > 0);
    CHECK(report::allFinite(document));

    // `--ideal` removes the noise: the two error outcomes disappear entirely.
    o.ideal = true;
    std::ostringstream ideal;
    REQUIRE(app::runHeadless(o, ideal, err) == 0);
    const core::Json idealCounts = core::Json::parse(ideal.str())["data"]["counts"]["counts"];
    CHECK(idealCounts.size() == 2);
    CHECK(idealCounts.contains("00"));
    CHECK(idealCounts.contains("11"));
}

TEST_CASE("headless --run resolves the program's pragmas where the command line pinned nothing") {
    // `bell.qasm` carries `pragma qlab.shots 1024`; without `--shots` that is what runs.
    app::Options o = parse({"--run", "x"});
    o.program = bellPath();
    o.json = true;
    std::ostringstream out, err;
    REQUIRE(app::runHeadless(o, out, err) == 0);
    CHECK(core::Json::parse(out.str())["data"]["shots"] == 1024);

    // With `--shots` the command line wins.
    app::Options pinned = parse({"--run", "x", "--shots", "128"});
    pinned.program = bellPath();
    pinned.json = true;
    std::ostringstream pinnedOut;
    REQUIRE(app::runHeadless(pinned, pinnedOut, err) == 0);
    CHECK(core::Json::parse(pinnedOut.str())["data"]["shots"] == 128);
}

TEST_CASE("headless --run reports a program that does not compile") {
    app::Options o = parse({"--run", "x"});
    o.program = core::assetDir() / "Programs" / "Conformance" / "semantics" / "err_undeclared.qasm";
    if (!std::filesystem::exists(o.program))
        SKIP("the conformance corpus is not installed");
    std::ostringstream out, err;
    CHECK(app::runHeadless(o, out, err) == 1);
    CHECK_FALSE(err.str().empty());
}

// The example oracle of spec 25 §2 lives in `ExampleTest.cpp` and runs the WHOLE corpus. The
// shortlist that used to stand here (bell, ghz_5, grover_3 on sc_fixed_5) passed while four other
// examples did not fit the default device and three read a register the checker never looked at.
