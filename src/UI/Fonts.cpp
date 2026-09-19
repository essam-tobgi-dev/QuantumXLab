// Spec 19 §1/§7 — the pinned font set (see Fonts.hpp).
#include "UI/Fonts.hpp"
#include "Core/Paths.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <imgui_freetype.h>

namespace qlab::ui {
namespace {

struct RoleSpec {
    FontRole role;
    std::string_view name;
    const char* file;
    float Metrics::* size;
};
constexpr std::array<RoleSpec, kFontRoleCount> kRoles{{
    {FontRole::Body, "body", "Inter-Regular.ttf", &Metrics::bodyPx},
    {FontRole::Secondary, "secondary", "Inter-Regular.ttf", &Metrics::secondaryPx},
    {FontRole::Strong, "strong", "Inter-SemiBold.ttf", &Metrics::bodyPx},
    {FontRole::PanelTitle, "panel_title", "Inter-SemiBold.ttf", &Metrics::panelTitlePx},
    {FontRole::Workspace, "workspace", "Inter-SemiBold.ttf", &Metrics::workspaceTitlePx},
    {FontRole::Code, "code", "JetBrainsMono-Regular.ttf", &Metrics::editorPx},
    {FontRole::CodeSmall, "code_small", "JetBrainsMono-Regular.ttf", &Metrics::logPx},
    {FontRole::Readout, "readout", "JetBrainsMono-Regular.ttf", &Metrics::readoutPx},
    // Rasterised at the workspace-title size and scaled by the layout engine, so the big
    // operators stay crisp when they are drawn 1.5–2× the text size.
    {FontRole::Math, "math", "LatinModernMath-Regular.otf", &Metrics::workspaceTitlePx},
}};

// Latin-1 plus the punctuation, arrows, Greek and mathematical marks the panels and the fallback
// math renderer draw (kets, ± ≈ ≥ · › — ⟨ ⟩ ħ ω ρ χ σ, the box-drawing used by the circuit ASCII
// export). Ranges are pairs, terminated by 0 (ImGui requires the array to outlive the atlas build).
// The LaTeX face carries the whole mathematical repertoire: operators, relations, arrows, big
// operators, delimiters, accents, and the italic/bold alphabets above U+FFFF (IMGUI_USE_WCHAR32).
constexpr std::array<ImWchar, 21> kMathGlyphRanges{
    0x0020,  0x00FF,  // Latin + Latin-1 (upright letters, digits, punctuation, ×, ±, µ)
    0x02B0,  0x036F,  // modifier letters and combining marks (˙ ¨ ¯ ˆ ˜ and the accents)
    0x0370,  0x03FF,  // Greek (upright)
    0x2000,  0x2BFF,  // punctuation, super/subscripts, letterlike, arrows, operators, technical,
                      // shapes
    0x1D400, 0x1D7FF, // mathematical alphanumerics: bold, italic, script, fraktur, double-struck,
                      // digits
    0,       0,       0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::array<ImWchar, 29> kGlyphRanges{
    0x0020, 0x00FF, // Latin + Latin-1 supplement
    0x0300, 0x036F, // combining marks (Q̇, n̄, X̄ in narration; zero-advance, drawn over the base)
    0x0370, 0x03FF, // Greek
    0x1E00, 0x1EFF, // Latin extended additional (ṅ — molar flow rate)
    0x2010, 0x206F, // general punctuation (— ‹ › ‰ ⁻ …)
    0x2070, 0x209F, // super/subscripts
    0x20A0, 0x20CF, // currency (µ-like symbols in some faces)
    0x2100, 0x21FF, // letterlike symbols and arrows (ħ ℏ → ↔)
    0x2200, 0x22FF, // mathematical operators (± ≈ ≤ ≥ ⊗ √ ∑)
    0x27E6, 0x27EB, // ⟦ ⟧ ⟨ ⟩
    0x2500, 0x257F, // box drawing
    0x25A0, 0x26FF, // geometric shapes and symbols (panel icons, spec 19 §1)
    0x2700, 0x27BF, // dingbats (✓ ❖ — panel icons; a test asserts every icon has a glyph)
    0x2B00, 0x2BFF, // miscellaneous symbols and arrows (⬒)
    0};

std::filesystem::path fontFile(const std::filesystem::path& dir, const char* name) {
    return dir / name;
}

} // namespace

std::string_view fontRoleName(FontRole r) {
    for (const RoleSpec& s : kRoles)
        if (s.role == r)
            return s.name;
    return "?";
}

std::filesystem::path FontSet::defaultDir() {
    if (const char* env = std::getenv("QXL_FONT_DIR"); env != nullptr && *env != '\0')
        return env;
    return core::assetDir() / "Fonts";
}

bool FontSet::available(const std::filesystem::path& fontDir) {
    const std::filesystem::path dir = fontDir.empty() ? defaultDir() : fontDir;
    std::error_code ec;
    for (const RoleSpec& s : kRoles)
        if (!std::filesystem::exists(fontFile(dir, s.file), ec))
            return false;
    return true;
}

Result<FontSet> FontSet::build(ImFontAtlas* atlas, const Theme& theme, float dpiScale,
                               float fontScale, const std::filesystem::path& fontDir) {
    if (atlas == nullptr)
        return fail(err::NoContext, "font set: no ImGui font atlas");
    const std::filesystem::path dir = fontDir.empty() ? defaultDir() : fontDir;
    std::error_code ec;
    for (const RoleSpec& s : kRoles)
        if (!std::filesystem::exists(fontFile(dir, s.file), ec))
            return fail(err::NoFont,
                        std::format("font '{}' is missing from {}", s.file, dir.string()));

    FontSet set;
    set.dpiScale = std::max(0.25f, dpiScale);
    set.fontScale = std::clamp(fontScale, 0.8f, 1.6f);
    const Metrics m = theme.metricsAt(set.dpiScale, set.fontScale);

    atlas->Clear();
    // Spec 19 §1: FreeType, hinting off, oversampling 3×1; no ImGui default font is ever added.
    atlas->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_NoHinting;
    for (const RoleSpec& s : kRoles) {
        ImFontConfig cfg;
        cfg.OversampleH = 3;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = false;
        cfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_NoHinting;
        cfg.GlyphRanges = s.role == FontRole::Math ? kMathGlyphRanges.data() : kGlyphRanges.data();
        std::snprintf(cfg.Name, sizeof(cfg.Name), "%.*s", static_cast<int>(s.name.size()),
                      s.name.data());
        const float px = std::round(m.*(s.size));
        ImFont* font = atlas->AddFontFromFileTTF(fontFile(dir, s.file).string().c_str(), px, &cfg);
        if (font == nullptr) {
            atlas->Clear();
            return fail(err::NoFont,
                        std::format("font '{}' could not be rasterised at {} px", s.file, px));
        }
        const auto i = static_cast<std::size_t>(s.role);
        set.faces[i] = font;
        set.sizes[i] = px;
    }
    if (!atlas->Build()) {
        atlas->Clear();
        return fail(err::NoFont, "the font atlas could not be built");
    }
    set.loaded = true;
    return set;
}

} // namespace qlab::ui
