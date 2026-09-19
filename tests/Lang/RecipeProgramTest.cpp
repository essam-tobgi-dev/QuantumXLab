// Spec 22 §6 — a shipped analysis recipe must be runnable: the calibration program it names has to
// exist, parse without an error diagnostic, and declare every `input` the recipe sweeps (spec 13
// §3, §7); when a recipe names no program it must say which family the runtime generates instead
// (interleaved RB and XEB — SPEC_DEVIATIONS.md #41). Six recipes shipped pointing at programs that
// did not exist; this test is what keeps that from coming back.
#include "Core/Json.hpp"
#include "Core/Paths.hpp"
#include "Lang/Lang.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace qlab;
namespace fs = std::filesystem;

namespace {
std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
std::vector<fs::path> jsonFiles(const fs::path& dir) {
    std::vector<fs::path> v;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".json")
            v.push_back(e.path());
    std::sort(v.begin(), v.end());
    return v;
}
} // namespace

TEST_CASE("every analysis recipe resolves to a program that parses, or to a generated family") {
    const fs::path dir = core::assetDir() / "Analysis";
    const fs::path root = core::assetDir().parent_path(); // recipe paths are repository-relative
    REQUIRE(fs::is_directory(dir));
    const auto files = jsonFiles(dir);
    REQUIRE(files.size() >= 18);
    int withProgram = 0, generated = 0;

    for (const auto& file : files) {
        INFO(file.string());
        const core::Json env = core::Json::parse(slurp(file));
        REQUIRE(env.contains("data"));
        const core::Json& d = env["data"];
        CHECK(!d.value("id", "").empty());
        REQUIRE(d.contains("sweep"));
        const core::Json& sweep = d["sweep"];

        if (!d.contains("program")) { // runtime-generated sequence family: say what, and why
            REQUIRE(d.contains("generator"));
            CHECK(!d["generator"].value("kind", "").empty());
            CHECK(!d.value("description", "").empty());
            ++generated;
            continue;
        }

        const std::string rel = d["program"].get<std::string>();
        CHECK(rel.starts_with("Assets/Programs/"));
        const fs::path path = root / rel;
        INFO("program " << path.string());
        REQUIRE(fs::exists(path));

        const lang::Program p = lang::analyzeProgram(slurp(path), path.filename().string());
        std::string diag;
        for (const auto& x : p.diagnostics)
            if (x.isError())
                diag += x.error.format() + "\n";
        INFO(diag);
        CHECK(p.ok());
        CHECK((p.hasQuantumStatements || p.pragmas.rb.has_value() || !p.defcals.empty()));

        if (p.pragmas.rb) {
            // `pragma qlab.rb <n> <lengths…> <samples>` generates the family (spec 13 §7), so the
            // swept variable is its sequence length: the recipe may only ask for lengths it emits.
            const auto& rb = *p.pragmas.rb;
            for (const auto& v : sweep["values"]) {
                INFO("rb length " << v.get<int>());
                CHECK(std::ranges::find(rb.lengths, v.get<int>()) != rb.lengths.end());
            }
            if (d["extract"].contains("sequences"))
                CHECK(d["extract"]["sequences"].get<int>() == rb.samples);
        } else {
            const auto declares = [&p](const std::string& name) {
                return std::ranges::any_of(
                    p.inputs, [&name](const lang::InputVar& v) { return v.name == name; });
            };
            for (const char* key : {"input", "input2"}) {
                if (!sweep.contains(key))
                    continue;
                const std::string name = sweep[key].get<std::string>();
                INFO("sweep " << key << " = " << name);
                CHECK(declares(name)); // an `input` the runtime can bind, not a display label
            }
            for (const auto& [name, spec] : d.value("inputs", core::Json::object()).items()) {
                INFO("fixed input " << name);
                CHECK(declares(name));
            }
        }
        ++withProgram;
    }
    CHECK(withProgram + generated == static_cast<int>(files.size()));
    CHECK(withProgram >= 16);
    CHECK(generated == 2); // rb_interleaved, xeb
}
