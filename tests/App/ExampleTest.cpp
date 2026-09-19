// Spec 25 §2/§6 — the shipped example corpus is a physics oracle, not a gallery: every program
// under `Assets/Programs/Examples` that ships a fixed distribution must reproduce it on the ideal
// backend, on the device its `pragma qlab.device` names or the smallest shipped device that holds
// it. A shortlist of three hand-picked examples used to stand in for the corpus and hid four
// programs that no longer fitted the default device and three whose expectation named a register
// the checker never read; this runs all of them.
#include "App/App.hpp"
#include "Core/Paths.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>

using namespace qlab;

namespace {

// Every `<name>.qasm` under the example root that ships a `<name>.expected.json`, in path order.
std::vector<std::filesystem::path> corpus() {
    std::vector<std::filesystem::path> out;
    const std::filesystem::path root = core::assetDir() / "Programs" / "Examples";
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
        return out;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".qasm")
            continue;
        std::filesystem::path expected = entry.path();
        expected.replace_extension(".expected.json");
        if (std::filesystem::exists(expected, ec))
            out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

app::Options selfTestOptions() {
    app::Options o;
    o.mode = app::Mode::SelfTest;
    return o; // `checkExample` pins the ideal backend, the shots, the seed and the device itself
}

app::ExampleCheck check(std::string_view category, std::string_view name) {
    const std::filesystem::path root = core::assetDir() / "Programs" / "Examples";
    return app::checkExample(root / category / (std::string(name) + ".qasm"), selfTestOptions());
}

std::filesystem::path scratch(std::string_view name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "qxl_app_test" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void write(const std::filesystem::path& file, std::string_view text) {
    std::ofstream out(file);
    out << text;
}

} // namespace

TEST_CASE("every shipped example reproduces its own expectation", "[oracle]") {
    const std::vector<std::filesystem::path> programs = corpus();
    REQUIRE(programs.size() >= 25);

    std::size_t compared = 0, skipped = 0;
    for (const std::filesystem::path& qasm : programs) {
        const app::ExampleCheck result = app::checkExample(qasm, selfTestOptions());
        INFO(result.name << " on '" << result.device << "': " << result.detail);
        if (result.skipped) {
            // A fit or a reconstruction is a legitimate oracle with no fixed distribution (spec 25
            // §3); nothing ran, so no device carried it.
            CHECK(result.device.empty());
            CHECK_FALSE(result.ran);
            ++skipped;
            continue;
        }
        CHECK(result.ran);
        CHECK(result.passed);
        CHECK_FALSE(result.device.empty());
        ++compared;
    }
    // Both kinds must be present: an all-skipped corpus would satisfy every assertion above.
    CHECK(compared >= 15);
    CHECK(skipped >= 5);
}

TEST_CASE("an example runs on the device it names, else the smallest that holds it", "[oracle]") {
    // Shipped data qubits, smallest first: sc_fixed_5 (5), ion_chain_11 (11), sc_heavyhex_27 (27),
    // ion_chain_32 (32), sc_tunable_grid_54 (54), sc_heavyhex_127 (127).
    CHECK(check("Basics", "bell").device == "sc_fixed_5");             // 2 qubits
    CHECK(check("QEC", "repetition_3_memory").device == "sc_fixed_5"); // 5 qubits: fits exactly
    CHECK(check("Oracles", "simon_6").device == "ion_chain_11");       // 6: the 5 does not fit
    CHECK(check("Oracles", "bernstein_vazirani_8").device == "ion_chain_11"); // 9 qubits
    CHECK(check("Search", "grover_6_two_marked").device == "ion_chain_11");   // 10 qubits
    // 12 qubits, and its own `pragma qlab.device` (spec 15 §1) wins over the size rule.
    CHECK(check("Factoring", "shor_15_order_finding").device == "ion_chain_32");
}

// No semicolon in the name: `catch_discover_tests` puts it in a CMake list, where `;` splits it.
TEST_CASE("an expectation reads the register it names, and a bare label spans the key",
          "[oracle]") {
    // Two one-bit registers: the run's key is "b a", so a one-bit label matches no key at all. That
    // is how teleportation, entanglement swapping and the repetition-code memory silently read
    // P = 0 for outcomes their programs produce on every shot.
    const std::filesystem::path dir = scratch("example_register");
    const std::filesystem::path qasm = dir / "two_registers.qasm";
    write(qasm, "OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit[2] q;\nbit a;\nbit b;\n"
                "h q[0];\na = measure q[0];\nb = measure q[1];\n");

    const auto expectation = [&](std::string_view extra) {
        static constexpr std::string_view kBody =
            R"({ "counts": { "0": 0.5, "1": 0.5 }, "tolerance": 1e-9, "shots": 4096, "seed": 1)";
        write(dir / "two_registers.expected.json", std::string(kBody) + std::string(extra) + " }");
    };

    expectation("");
    const app::ExampleCheck narrow = app::checkExample(qasm, selfTestOptions());
    INFO(narrow.detail);
    CHECK(narrow.ran);
    CHECK_FALSE(narrow.passed);
    CHECK(narrow.detail.find("does not span the oracle") != std::string::npos);
    CHECK(narrow.detail.find("1 bits against 2") != std::string::npos);

    // Named, the same labels are the marginal over `a`: q[0] is |+>, q[1] is untouched.
    expectation(R"(, "register": "a")");
    const app::ExampleCheck named = app::checkExample(qasm, selfTestOptions());
    INFO(named.detail);
    CHECK(named.passed);

    // `b` is measured in |0> on every shot, so its marginal is (1, 0) and a claim of 0.5 fails.
    expectation(R"(, "register": "b")");
    const app::ExampleCheck wrong = app::checkExample(qasm, selfTestOptions());
    INFO(wrong.detail);
    CHECK(wrong.ran);
    CHECK_FALSE(wrong.passed);

    // A register the program does not declare is named in the failure, not read as zero.
    expectation(R"(, "register": "out")");
    const app::ExampleCheck missing = app::checkExample(qasm, selfTestOptions());
    INFO(missing.detail);
    CHECK_FALSE(missing.passed);
    CHECK(missing.detail.find("a[1], b[1]") != std::string::npos);
}
