// Spec 19 (opening paragraph) and 03 §4 — "All ImGui calls live in src/UI/; the lint fails the
// build on `ImGui::` or `ImPlot::` outside it." The allowed directories are `src/UI`, `src/Viz`
// (the state views draw themselves, spec 21 §1.2) and `src/App` (it owns the backends).
// The same walk checks the layering rule of spec 02 §1: layers 0–3 include no ImGui, GLFW or GL
// header at all.
#include "Core/Paths.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

fs::path sourceRoot() { return fs::path(QXL_SOURCE_DIR) / "src"; }

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// The first path component under src/ ("UI", "Viz", "QSim", …).
std::string moduleOf(const fs::path& file) {
    const fs::path rel = fs::relative(file, sourceRoot());
    return rel.empty() ? std::string{} : rel.begin()->string();
}

bool isSource(const fs::path& p) {
    const std::string ext = p.extension().string();
    return ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc";
}

std::vector<fs::path> allSources() {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(sourceRoot(), ec))
        if (entry.is_regular_file() && isSource(entry.path())) out.push_back(entry.path());
    std::sort(out.begin(), out.end());
    return out;
}

// Lines that hold `needle` outside a `//` comment (the rule is about calls, not prose).
std::vector<std::string> codeHits(const std::string& text, std::string_view needle) {
    std::vector<std::string> hits;
    std::istringstream in(text);
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        const std::size_t at = line.find(needle);
        if (at == std::string::npos) continue;
        const std::size_t comment = line.find("//");
        if (comment != std::string::npos && comment < at) continue;
        hits.push_back(std::to_string(number) + ": " + line);
    }
    return hits;
}

// `"#RGB"`, `"#RRGGBB"` or `"#RRGGBBAA"` string literals — a hex colour written into the code.
std::vector<std::string> hexColorHits(const std::string& text) {
    std::vector<std::string> hits;
    std::istringstream in(text);
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        for (std::size_t at = line.find("\"#"); at != std::string::npos; at = line.find("\"#", at + 2)) {
            std::size_t end = at + 2;
            while (end < line.size() && std::isxdigit(static_cast<unsigned char>(line[end])) != 0) ++end;
            const std::size_t digits = end - at - 2;
            if (end < line.size() && line[end] == '"' && (digits == 3 || digits == 6 || digits == 8))
                hits.push_back(std::to_string(number) + ": " + line);
        }
    }
    return hits;
}

} // namespace

TEST_CASE("Lint: ImGui and ImPlot are called only from src/UI, src/Viz and src/App (spec 19, 03 §4)") {
    const std::set<std::string> allowed{"UI", "Viz", "App"};
    std::vector<std::string> offences;
    std::size_t scanned = 0, uiCalls = 0;
    for (const fs::path& file : allSources()) {
        const std::string module = moduleOf(file);
        const std::string text = readAll(file);
        ++scanned;
        const auto imgui = codeHits(text, "ImGui::");
        const auto implot = codeHits(text, "ImPlot::");
        if (allowed.contains(module)) {
            uiCalls += imgui.size() + implot.size();
            continue;
        }
        for (const std::string& hit : imgui) offences.push_back(file.string() + ":" + hit);
        for (const std::string& hit : implot) offences.push_back(file.string() + ":" + hit);
    }
    REQUIRE(scanned > 100);            // the walk really found the tree
    CHECK(uiCalls > 200);              // …and the allowed modules really do call ImGui
    const std::string first = offences.empty() ? std::string{} : offences.front();
    INFO(first);
    CHECK(offences.empty());
}

TEST_CASE("Lint: layers 0-3 include no ImGui, GLFW or OpenGL header (spec 02 §1)") {
    // Everything except the presentation layers must build and test headless.
    const std::set<std::string> presentation{"UI", "Viz", "Graphics", "Lab", "App", "Report"};
    std::vector<std::string> offences;
    for (const fs::path& file : allSources()) {
        if (presentation.contains(moduleOf(file))) continue;
        const std::string text = readAll(file);
        for (std::string_view header : {"#include <imgui", "#include <implot", "#include <GLFW/", "#include <OpenGL/",
                                        "#include \"imgui", "#include <glad"})
            for (const std::string& hit : codeHits(text, header)) offences.push_back(file.string() + ":" + hit);
    }
    const std::string first = offences.empty() ? std::string{} : offences.front();
    INFO(first);
    CHECK(offences.empty());
}

TEST_CASE("Lint: no widget carries a literal colour (spec 19 §1)") {
    // Every colour in src/UI comes from a theme token. The two files that MAY name a hex colour are
    // the theme itself (the §1 fallback tables) and its ImGui style bridge.
    const std::set<std::string> exempt{"Theme.cpp", "Theme.hpp", "ThemeStyle.cpp"};
    std::vector<std::string> offences;
    for (const fs::path& file : allSources()) {
        if (moduleOf(file) != "UI" || exempt.contains(file.filename().string())) continue;
        const std::string text = readAll(file);
        for (const std::string& hit : hexColorHits(text)) offences.push_back(file.string() + ":" + hit);
        for (const std::string& hit : codeHits(text, "IM_COL32")) offences.push_back(file.string() + ":" + hit);
    }
    const std::string first = offences.empty() ? std::string{} : offences.front();
    INFO(first);
    CHECK(offences.empty());
}
