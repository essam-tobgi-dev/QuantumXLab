// Spec 13 §9–§10 / 25 §5: every example, calibration and conformance program is parsed; conformance
// programs must produce exactly the diagnostic ids listed in their .diag.json.
#include "Core/Json.hpp"
#include "Core/Paths.hpp"
#include "Lang/Lang.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
using namespace qlab;
using namespace qlab::lang;
namespace fs = std::filesystem;

static std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static std::vector<fs::path> qasmFiles(const fs::path& dir) {
    std::vector<fs::path> v;
    if (!fs::exists(dir))
        return v;
    for (auto& e : fs::recursive_directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".qasm")
            v.push_back(e.path());
    std::sort(v.begin(), v.end());
    return v;
}

TEST_CASE("every example and calibration program parses without error diagnostics") {
    fs::path root = core::assetDir() / "Programs";
    auto files = qasmFiles(root / "Examples");
    auto cal = qasmFiles(root / "Calibration");
    files.insert(files.end(), cal.begin(), cal.end());
    REQUIRE(files.size() >= 38);
    for (auto& f : files) {
        Program p = analyzeProgram(slurp(f), f.filename().string());
        std::string diag;
        for (auto& d : p.diagnostics)
            if (d.isError())
                diag += d.error.format() + "\n";
        INFO(f.string() << "\n" << diag);
        CHECK(p.ok());
        CHECK((p.hasQuantumStatements || p.pragmas.rb.has_value() || !p.defcals.empty()));
        if (f.parent_path().filename() == "Examples" ||
            f.parent_path().parent_path().filename() == "Examples") {
            fs::path exp = f;
            exp.replace_extension(".expected.json");
            CHECK(fs::exists(exp));
        }
    }
}

TEST_CASE("conformance corpus matches expected diagnostics") {
    fs::path root = core::assetDir() / "Programs" / "Conformance";
    auto files = qasmFiles(root);
    REQUIRE(files.size() >= 40);
    int checked = 0;
    for (auto& f : files) {
        fs::path dj = f;
        dj.replace_extension(".diag.json");
        REQUIRE(fs::exists(dj));
        core::Json j = core::Json::parse(slurp(dj));
        std::vector<std::string> expected = j["diagnostics"].get<std::vector<std::string>>();
        Program p = analyzeProgram(slurp(f), f.filename().string());
        std::vector<std::string> got;
        std::string text;
        for (auto& d : p.diagnostics) {
            got.push_back(d.id());
            text += d.error.format() + "\n";
        }
        std::sort(expected.begin(), expected.end());
        std::sort(got.begin(), got.end());
        INFO(f.string() << "\n" << text);
        CHECK(got == expected);
        ++checked;
    }
    REQUIRE(checked == static_cast<int>(files.size()));
}

TEST_CASE("dumpAst is deterministic and stdgates table is complete") {
    std::string src = slurp(core::assetDir() / "Programs" / "Examples" / "Basics" / "bell.qasm");
    Program a = analyzeProgram(src, "bell.qasm"), b = analyzeProgram(src, "bell.qasm");
    REQUIRE(dumpAst(a.ast) == dumpAst(b.ast));
    for (const char* g :
         {"p",   "x",     "y",  "z",  "h",     "s",  "sdg", "t",   "tdg", "sx", "rx",
          "ry",  "rz",    "cx", "cy", "cz",    "cp", "crx", "cry", "crz", "ch", "swap",
          "ccx", "cswap", "cu", "CX", "phase", "id", "u1",  "u2",  "u3",  "U",  "gphase"})
        REQUIRE(StdGates::find(g) != nullptr);
    REQUIRE(StdGates::doc("cx")->signature == "cx c, t");
    Program inc = analyzeProgram(
        std::string("OPENQASM 3.0;\nqubit q;\n") +
        std::string(StdGates::source()).substr(std::string(StdGates::source()).find('\n') + 1) +
        "x q;\n");
    // stdgates.inc source itself only redefines std names -> QL3061 for each definition is expected
    bool allShadow = true;
    for (auto& d : inc.diagnostics)
        if (d.id() != "QL3061" && d.isError())
            allShadow = false;
    REQUIRE(allShadow);
    REQUIRE(Diagnostics::info("QL3150").severity == Severity::Error);
    REQUIRE(fs::exists(core::assetDir() / "Lang" / "diagnostics.json"));
}
