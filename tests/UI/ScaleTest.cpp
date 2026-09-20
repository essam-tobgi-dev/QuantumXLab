// Spec 19 §7 — "the interface is laid out in logical points; the display scale only decides how
// finely the faces are rasterised." Dear ImGui's GLFW backend keeps `io.DisplaySize` in window
// points and puts the framebuffer ratio in `io.DisplayFramebufferScale`, so the platform already
// magnifies everything the UI submits. Code that ALSO multiplies a size by the display scale draws
// the whole interface twice as large on a Retina display — the defect these tests pin down.
//
// Two independent guards:
//   * a behavioural one — the same frame at 1x and at 2x must lay out to the same logical geometry;
//   * a source one — `dpiScale` may only be named where a logical->device conversion belongs.
#include "Core/Paths.hpp"
#include "UI/Widgets/Widgets.hpp"
#include "UiHarness.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>
#include <fstream>
#include <imgui_internal.h> // FindWindowByName: the test reads back the laid-out windows
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace qlab;
using namespace qlab::ui;

namespace {

namespace fs = std::filesystem;

// Everything about a frame that must not depend on the display scale, in logical points.
struct Layout {
    float fontSize = 0.0f; // ImGui::GetFontSize() — the DISPLAY size of the body face
    ImVec2 framePadding{}, itemSpacing{}, windowPadding{};
    float frameRounding = 0.0f, scrollbar = 0.0f, indent = 0.0f;
    float buttonWidth = 0.0f, buttonHeight = 0.0f;
    float bodyPx = 0.0f, gutterPx = 0.0f;  // ctx.metrics_px(): the theme as the panels read it
    std::map<std::string, ImVec2> content; // per window: laid-out content size
};

Layout measure(test::UiHarness& ui, Shell& shell, float dpiScale, float fontScale = 1.0f) {
    ImGuiIO& io = ImGui::GetIO();
    REQUIRE(ui.resources().applyScale(io.Fonts, dpiScale, fontScale));
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h); // rebuild the atlas; nothing uploads it
    ui.resources().bind(ui.context(), dpiScale, fontScale);

    Layout out;
    // Three frames: build the dock layout, draw into it, prove it settled.
    for (int frame = 0; frame < 3; ++frame) {
        ui.frame([&] {
            shell.draw(ui.context());
            // A button of known text, measured the way a panel would get it.
            ImGui::Begin("##scale_probe");
            (void)widgets::primaryButton(ui.context(), "Run  F5");
            out.buttonWidth = ImGui::GetItemRectSize().x;
            out.buttonHeight = ImGui::GetItemRectSize().y;
            out.fontSize = ImGui::GetFontSize();
            ImGui::End();
        });
    }
    const ImGuiStyle& s = ImGui::GetStyle();
    out.framePadding = s.FramePadding;
    out.itemSpacing = s.ItemSpacing;
    out.windowPadding = s.WindowPadding;
    out.frameRounding = s.FrameRounding;
    out.scrollbar = s.ScrollbarSize;
    out.indent = s.IndentSpacing;
    const Metrics m = ui.context().metrics_px();
    out.bodyPx = m.bodyPx;
    out.gutterPx = m.spacing(4);
    for (const PanelPtr& p : shell.panels())
        if (const ImGuiWindow* win = ImGui::FindWindowByName(p->windowTitle().c_str());
            win != nullptr && win->WasActive)
            out.content[std::string(p->key())] = win->ContentSize;
    return out;
}

// Lines holding `needle` outside a `//` comment (the rule is about code, not prose).
std::vector<int> codeHits(const fs::path& file, std::string_view needle) {
    std::ifstream in(file);
    std::vector<int> hits;
    std::string line;
    for (int number = 1; std::getline(in, line); ++number) {
        const std::size_t at = line.find(needle);
        if (at == std::string::npos)
            continue;
        const std::size_t comment = line.find("//");
        if (comment == std::string::npos || at < comment)
            hits.push_back(number);
    }
    return hits;
}

} // namespace

TEST_CASE("Scale: the same frame lays out identically at 1x and 2x (spec 19 §7)", "[ui][scale]") {
    test::UiHarness ui;
    if (!ui.ready())
        SKIP("the pinned fonts are not available");
    Shell shell;
    shell.setWorkspace(Workspace::Program);

    const Layout one = measure(ui, shell, 1.0f);
    const Layout two = measure(ui, shell, 2.0f);

    using Catch::Matchers::WithinAbs;
    // The style is computed, not measured: it must match exactly (to float rounding).
    CHECK_THAT(two.framePadding.x, WithinAbs(one.framePadding.x, 0.01));
    CHECK_THAT(two.framePadding.y, WithinAbs(one.framePadding.y, 0.01));
    CHECK_THAT(two.itemSpacing.x, WithinAbs(one.itemSpacing.x, 0.01));
    CHECK_THAT(two.itemSpacing.y, WithinAbs(one.itemSpacing.y, 0.01));
    CHECK_THAT(two.windowPadding.x, WithinAbs(one.windowPadding.x, 0.01));
    CHECK_THAT(two.frameRounding, WithinAbs(one.frameRounding, 0.01));
    CHECK_THAT(two.scrollbar, WithinAbs(one.scrollbar, 0.01));
    CHECK_THAT(two.indent, WithinAbs(one.indent, 0.01));
    CHECK_THAT(two.bodyPx, WithinAbs(one.bodyPx, 0.01));
    CHECK_THAT(two.gutterPx, WithinAbs(one.gutterPx, 0.01));
    // The face is rasterised twice as finely at 2x, so io.FontGlobalScale must bring its DISPLAY
    // size back to the same logical value. Without it every glyph, and so every control sized from
    // the text, comes out twice as big.
    CHECK_THAT(two.fontSize, WithinAbs(one.fontSize, 0.01));
    // A button is text plus padding: the advance widths differ slightly between rasterisations, so
    // allow a point, not a factor.
    CHECK_THAT(two.buttonWidth, WithinAbs(one.buttonWidth, 1.5));
    CHECK_THAT(two.buttonHeight, WithinAbs(one.buttonHeight, 1.5));
    CHECK(one.buttonWidth > 0.0f);

    // Every panel's laid-out content, the part a stray `* dpiScale` inside a widget would double.
    // 6 % + 4 pt absorbs a wrap that falls differently between rasterisations; a doubling does not
    // come close to fitting in it.
    REQUIRE(one.content.size() >= 6);
    REQUIRE(two.content.size() == one.content.size());
    for (const auto& [key, size] : one.content) {
        INFO("panel " << key);
        const auto at = two.content.find(key);
        REQUIRE(at != two.content.end());
        CHECK(at->second.x <= size.x * 1.06f + 4.0f);
        CHECK(at->second.x >= size.x / 1.06f - 4.0f);
        CHECK(at->second.y <= size.y * 1.06f + 4.0f);
        CHECK(at->second.y >= size.y / 1.06f - 4.0f);
    }
}

TEST_CASE("Scale: the user text scale is the one knob that magnifies the interface (spec 19 §7)",
          "[ui][scale]") {
    test::UiHarness ui;
    if (!ui.ready())
        SKIP("the pinned fonts are not available");
    Shell shell;
    shell.setWorkspace(Workspace::Program);

    const Layout plain = measure(ui, shell, 2.0f, 1.0f);
    const Layout large = measure(ui, shell, 2.0f, 1.3f);
    using Catch::Matchers::WithinRel;
    CHECK_THAT(large.fontSize, WithinRel(plain.fontSize * 1.3f, 0.03f));
    CHECK_THAT(large.bodyPx, WithinRel(plain.bodyPx * 1.3f, 0.01f));
    CHECK_THAT(large.framePadding.x, WithinRel(plain.framePadding.x * 1.3f, 0.02f));
    CHECK_THAT(large.itemSpacing.y, WithinRel(plain.itemSpacing.y * 1.3f, 0.02f));
    CHECK(large.buttonWidth > plain.buttonWidth * 1.15f);
    CHECK(large.buttonHeight > plain.buttonHeight * 1.15f);

    // And the Shell keeps it inside the spec's range whatever it is asked for.
    shell.setFontScale(5.0f);
    CHECK(shell.fontScale() == 1.6f);
    shell.setFontScale(0.1f);
    CHECK(shell.fontScale() == 0.8f);
    shell.setFontScale(1.15f);
    CHECK(shell.fontScale() == 1.15f);
}

TEST_CASE("Scale: `dpiScale` is named only where a logical->device conversion belongs",
          "[ui][scale]") {
    // The scale reaches exactly two kinds of code: the places that own it (the context, the font
    // atlas, the theme) and the places that hand a PIXEL count to something outside ImGui — the 3D
    // viewport's framebuffer and pick ray, and the state views' GL canvases. A widget size is in
    // logical points and must never be multiplied by it.
    const std::set<std::string> owners{
        "UI/Context.cpp", "UI/Context.hpp", "UI/UI.cpp",    "UI/UI.hpp",         "UI/Fonts.cpp",
        "UI/Fonts.hpp",   "UI/Theme.cpp",   "UI/Theme.hpp", "UI/ThemeStyle.cpp",
    };
    const std::set<std::string> converters{
        "UI/Panels/ViewportPanel.cpp", // framebuffer size and pick coordinates
        "Viz/IStateView.hpp",          // the field itself
        "Viz/Views/BlochViewDraw.cpp", "Viz/Views/CityView.cpp", "Viz/Views/CircuitViewPanel.cpp",
        "Viz/Views/GraphView.cpp",
        "Viz/Views/QSphereView.cpp", // GL canvas resolution
    };
    const fs::path root = fs::path(QXL_SOURCE_DIR) / "src";
    std::vector<std::string> offenders;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
        if (!entry.is_regular_file())
            continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".hpp")
            continue;
        const std::string rel = fs::relative(entry.path(), root).generic_string();
        if (owners.contains(rel) || converters.contains(rel))
            continue;
        for (int line : codeHits(entry.path(), "dpiScale"))
            offenders.push_back(rel + ":" + std::to_string(line));
    }
    // App holds the scale and passes it on; that is ownership, not layout.
    std::erase_if(offenders, [](const std::string& s) { return s.starts_with("App/"); });
    INFO("a size in logical points must not be multiplied by the display scale; use ctx.ui(x):\n  "
         << [&] {
                std::ostringstream o;
                for (const std::string& s : offenders)
                    o << s << "\n  ";
                return o.str();
            }());
    CHECK(offenders.empty());
}
