// Spec 19 §1/§7/§8/§9 — the theme tokens, the HiDPI scale, the accessibility contrast rule and the
// acceptance check that no ImGui default colour, rounding or padding survives.
#include "UI/Theme.hpp"
#include "Data/Fidelity.hpp"
#include "UI/UI.hpp"
#include "UiHarness.hpp"
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace qlab;
using namespace qlab::ui;
using Catch::Approx;

namespace {

// WCAG 2.1 relative luminance / contrast, computed here independently of the implementation so the
// test is an oracle rather than a mirror.
double luminance(const Color& c) {
    const auto channel = [](float v) {
        const double s = static_cast<double>(v);
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}
double contrast(const Color& a, const Color& b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

} // namespace

TEST_CASE("Theme: the spec 19 §1 tokens load from theme.json and match the compiled table") {
    const auto dark = Theme::load("dark");
    REQUIRE(dark.has_value());
    CHECK(dark->dark());
    // The §1 table, verbatim.
    CHECK(dark->color(Token::BgBase) == Theme::parseHex("#0F1115").value());
    CHECK(dark->color(Token::BgPanel) == Theme::parseHex("#161A20").value());
    CHECK(dark->color(Token::TextPrimary) == Theme::parseHex("#E6E9EF").value());
    CHECK(dark->color(Token::Accent) == Theme::parseHex("#4FA3FF").value());
    // `accent.soft` carries its alpha: #4FA3FF33 = 0x33/255.
    CHECK(dark->color(Token::AccentSoft).a == Approx(51.0 / 255.0).margin(1e-6));
    CHECK(dark->fidelityColor(data::FidelityClass::Exact) == dark->color(Token::ClassExact));
    CHECK(dark->fidelityColor(data::FidelityClass::Illustrative) ==
          dark->color(Token::ClassIllustrative));

    const auto light = Theme::load("light");
    REQUIRE(light.has_value());
    CHECK_FALSE(light->dark());
    CHECK(light->color(Token::BgPanel) == Theme::parseHex("#FFFFFF").value());
    CHECK(light->color(Token::Accent) == Theme::parseHex("#1F6FEB").value());

    // A malformed palette name is an error naming it; a malformed colour names the token.
    core::Json bad = core::Json::object();
    bad["palettes"] = core::Json::object();
    bad["palettes"]["dark"] = core::Json::object();
    bad["palettes"]["dark"]["bg.panel"] = "not a colour";
    const auto failed = Theme::fromJson(bad, "dark");
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().message.find("not a colour") != std::string::npos);
}

TEST_CASE(
    "Theme: the qubit palette is Okabe-Ito and wraps with a lightness step (spec 19 §1, §8)") {
    const Theme dark = Theme::fallback(true);
    constexpr std::array<const char*, 8> kOkabeIto{"#E69F00", "#56B4E9", "#009E73", "#F0E442",
                                                   "#0072B2", "#D55E00", "#CC79A7", "#999999"};
    for (std::uint32_t i = 0; i < 8; ++i)
        CHECK(dark.qubitColor(i) == Theme::parseHex(kOkabeIto[i]).value());
    // Qubit 8 reuses hue 0 one lightness step lighter in the dark theme.
    const Color base = dark.qubitColor(0), wrapped = dark.qubitColor(8);
    CHECK(wrapped != base);
    CHECK(luminance(wrapped) > luminance(base));
    const Theme light = Theme::fallback(false);
    CHECK(luminance(light.qubitColor(8)) < luminance(light.qubitColor(0)));
}

TEST_CASE("Theme: every text/background pair reads at >= 4.5:1 (spec 19 §8)") {
    for (const char* name : {"dark", "light"}) {
        const Theme theme =
            Theme::load(name).value_or(Theme::fallback(std::string_view(name) == "dark"));
        INFO("palette " << name);
        for (Token bg : backgroundTokens()) {
            for (Token fg : textTokens()) {
                // What is actually drawn goes through the contrast helper (spec 19 §8; the pinned
                // §1 hues `ok` and `warn` sit below 4.5:1 on white in the light palette).
                const Color drawn = theme.textOn(fg, bg);
                INFO(tokenName(fg) << " on " << tokenName(bg));
                CHECK(contrast(drawn, theme.color(bg)) >= 4.5);
            }
        }
        // The two pure text tones clear the bar with the palette value itself, unmodified.
        for (Token bg : backgroundTokens()) {
            CHECK(theme.textOn(Token::TextPrimary, bg) == theme.color(Token::TextPrimary));
            CHECK(theme.textOn(Token::TextSecondary, bg) == theme.color(Token::TextSecondary));
            CHECK(theme.contrast(Token::TextPrimary, bg) >= 12.0);
            CHECK(theme.contrast(Token::TextSecondary, bg) >= 4.5);
        }
        // `text.disabled` is exempt: WCAG 1.4.3 excludes inactive components. It must still be
        // clearly the dimmest of the three text tones.
        CHECK(theme.contrast(Token::TextDisabled, Token::BgPanel) <
              theme.contrast(Token::TextSecondary, Token::BgPanel));
    }
    // The measured values of the §8 sentence, on bg.panel.
    CHECK(Theme::load("dark").value().contrast(Token::TextSecondary, Token::BgPanel) ==
          Approx(6.86).margin(0.02));
    CHECK(Theme::load("light").value().contrast(Token::TextSecondary, Token::BgPanel) ==
          Approx(5.98).margin(0.02));
}

TEST_CASE("Theme: readableText is idempotent and monotone") {
    const Theme light = Theme::fallback(false);
    const Color bg = light.color(Token::BgPanel);
    const Color once = Theme::readableText(light.color(Token::Ok), bg);
    CHECK(contrast(once, bg) >= 4.5);
    CHECK(Theme::readableText(once, bg) == once);      // already readable: unchanged
    CHECK(contrast(light.color(Token::Ok), bg) < 4.5); // the pinned hue is what needed help
    // A pair that already clears a higher bar is returned untouched.
    CHECK(Theme::readableText(light.color(Token::TextPrimary), bg) ==
          light.color(Token::TextPrimary));
}

TEST_CASE("Theme: pixel tokens scale with the DPI and the user font scale (spec 19 §7)") {
    const Theme t = Theme::load("dark").value_or(Theme::fallback(true));
    const Metrics base = t.metrics();
    CHECK(base.bodyPx == Approx(13.0f));
    CHECK(base.panelTitlePx == Approx(15.0f));
    CHECK(base.editorPx == Approx(13.0f));
    CHECK(base.spacing(1) == Approx(4.0f));
    CHECK(base.spacing(6) == Approx(32.0f));
    CHECK(base.spacing(0) == Approx(4.0f));  // clamped
    CHECK(base.spacing(9) == Approx(32.0f)); // clamped

    const Metrics retina = t.metricsAt(2.0f);
    CHECK(retina.bodyPx == Approx(26.0f));
    CHECK(retina.radiusMd == Approx(2.0f * base.radiusMd));
    // The user font scale multiplies again and is clamped to 0.8 … 1.6.
    CHECK(t.metricsAt(2.0f, 1.5f).bodyPx == Approx(39.0f));
    CHECK(t.metricsAt(1.0f, 4.0f).bodyPx == Approx(base.bodyPx * 1.6f));
    CHECK(t.metricsAt(1.0f, 0.1f).bodyPx == Approx(base.bodyPx * 0.8f));
}

TEST_CASE("Theme: the ImGui style carries no default value (spec 19 §9)") {
    test::UiHarness ui;
    if (!ui.ready())
        SKIP("the pinned fonts are not available");
    const Theme& theme = ui.resources().theme;
    // `applyScale` applied the style; nothing may still hold an ImGui default.
    const std::vector<std::string> mismatches = imguiStyleMismatches(theme, 1.0f);
    const std::string firstMismatch = mismatches.empty() ? std::string{} : mismatches.front();
    INFO(firstMismatch);
    CHECK(mismatches.empty());

    // A pristine style must differ everywhere it matters, i.e. the check has teeth.
    ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiStyle saved = style;
    style = ImGuiStyle{};
    CHECK_FALSE(imguiStyleMismatches(theme, 1.0f).empty());
    style = saved;
    CHECK(imguiStyleMismatches(theme, 1.0f).empty());

    // The §1 tokens reach the slots that matter.
    const auto toColor = [](const ImVec4& v) { return Color(v.x, v.y, v.z, v.w); };
    CHECK(toColor(style.Colors[ImGuiCol_Text]) == theme.color(Token::TextPrimary));
    CHECK(toColor(style.Colors[ImGuiCol_WindowBg]) == theme.color(Token::BgBase));
    CHECK(toColor(style.Colors[ImGuiCol_Border]) == theme.color(Token::Border));
    CHECK(style.TabRounding == Approx(theme.metrics().radiusSm)); // spec 19 §4
}

TEST_CASE("Theme: the Viz variant carries the same tokens (spec 21 §1)") {
    const Theme t = Theme::load("dark").value_or(Theme::fallback(true));
    const viz::VizTheme v = t.viz();
    CHECK(v.dark);
    CHECK(v.bgPanel == t.color(Token::BgPanel));
    CHECK(v.accent == t.color(Token::Accent));
    CHECK(v.simOnly == t.color(Token::SimOnly));
    CHECK(v.fidelityColor(data::FidelityClass::Statistical) == t.color(Token::ClassStatistical));
    for (std::uint32_t q = 0; q < 8; ++q)
        CHECK(v.qubitColor(q) == t.qubitColor(q));
    CHECK(v.fontPx == Approx(t.metrics().bodyPx));
}

TEST_CASE("every panel icon has a glyph in the shipped interface font") {
    // Regression: sixteen of the nineteen panel icons were code points Inter does not contain, so
    // every panel tab began with a "?" box. The atlas loading the range is not enough — the face
    // has to carry the glyph.
    ImFontAtlas atlas;
    auto theme = Theme::load("dark");
    REQUIRE(theme);
    auto fonts = FontSet::build(&atlas, *theme);
    if (!fonts || !fonts->loaded)
        SKIP("the shipped fonts are not available");
    ImFont* title = fonts->get(FontRole::PanelTitle);
    REQUIRE(title != nullptr);

    std::vector<std::string> missing;
    for (const auto& panel : makeAllPanels()) {
        const std::string icon(panel->icon());
        REQUIRE_FALSE(icon.empty());
        // Decode the single UTF-8 code point the icon is.
        // Decode one UTF-8 code point (the icons are all in the basic multilingual plane).
        unsigned cp = 0;
        const unsigned char* b = reinterpret_cast<const unsigned char*>(icon.c_str());
        if (b[0] < 0x80)
            cp = b[0];
        else if ((b[0] & 0xE0) == 0xC0)
            cp = ((b[0] & 0x1Fu) << 6) | (b[1] & 0x3Fu);
        else if ((b[0] & 0xF0) == 0xE0)
            cp = ((b[0] & 0x0Fu) << 12) | ((b[1] & 0x3Fu) << 6) | (b[2] & 0x3Fu);
        REQUIRE(cp != 0);
        if (title->FindGlyphNoFallback(static_cast<ImWchar>(cp)) == nullptr)
            missing.push_back(std::string(panel->key()) + " '" + icon + "'");
    }
    for (const auto& m : missing)
        UNSCOPED_INFO("no glyph: " << m);
    CHECK(missing.empty());
}

TEST_CASE("the interface font carries the physics glyphs the narration uses (ṅ, combining dot, "
          "subscripts)") {
    // A missing glyph renders as `?` in a tooltip or a tour card: "Q?" for Q̇, "?₃" for ṅ₃.
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    qlab::ui::Theme theme = qlab::ui::Theme::fallback();
    auto set = qlab::ui::FontSet::build(io.Fonts, theme, 1.0f, 1.0f);
    if (!set) {
        ImGui::DestroyContext();
        SKIP("the pinned fonts are not available");
    }
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    ImFont* body = set->get(qlab::ui::FontRole::Body);
    REQUIRE(body != nullptr);
    for (ImWchar cp : {ImWchar(0x1E45), ImWchar(0x0307), ImWchar(0x0304), ImWchar(0x2083),
                       ImWchar(0x00B2), ImWchar(0x03B7)}) {
        INFO("code point U+" << std::hex << cp);
        CHECK(body->FindGlyphNoFallback(cp) != nullptr);
    }
    ImGui::DestroyContext();
}
