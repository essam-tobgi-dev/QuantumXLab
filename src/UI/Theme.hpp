#pragma once
// Spec 19 §1 — the design-system tokens. Every colour, radius, spacing step and font size the UI
// draws with is loaded once from `Assets/Lang/theme.json` into this object; no widget holds a
// literal colour. `viz::VizTheme` is the same table narrowed to what the state views need, so the
// UI hands out `theme.viz()` rather than passing colours in one by one (spec 21 §1).
//
// ImGui does not appear here: `applyImGuiStyle` / `imguiStyleMatches` are implemented in
// ThemeStyle.cpp so that this header stays usable from headless code and from the tests.
#include "Core/Error.hpp"
#include "Core/Json.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Theme.hpp"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::ui {

using Color = glm::vec4;

// Error codes owned by this module (ErrorCode::Ui_ block, spec 04 §2).
namespace err {
inline constexpr ErrorCode BadToken = ErrorCode::Ui_ + 1;     // malformed colour / unknown token
inline constexpr ErrorCode BadAsset = ErrorCode::Ui_ + 2;     // theme.json / strings.en.json problem
inline constexpr ErrorCode NoFont = ErrorCode::Ui_ + 3;       // a pinned font file is missing
inline constexpr ErrorCode NoContext = ErrorCode::Ui_ + 4;    // an ImGui/GL call without a context
inline constexpr ErrorCode UnknownPanel = ErrorCode::Ui_ + 5;
inline constexpr ErrorCode BadLayout = ErrorCode::Ui_ + 6;    // layout JSON does not match the schema
} // namespace err

// The §1 table, in table order. `Count` is the number of colour tokens.
enum class Token : std::uint8_t {
    BgBase, BgPanel, BgRaised, BgViewport, Border,
    TextPrimary, TextSecondary, TextDisabled,
    Accent, AccentSoft, Ok, Warn, Err, SimOnly,
    ClassExact, ClassNumerical, ClassStatistical, ClassModel, ClassIllustrative,
    Count
};
inline constexpr std::size_t kTokenCount = static_cast<std::size_t>(Token::Count);

// "bg.panel", "text.secondary", "class.exact", … exactly as `theme.json` spells them.
std::string_view tokenName(Token t);
std::optional<Token> tokenFromName(std::string_view name);
// The tokens the UI renders TEXT in (spec 19 §8 contrast rule) and the background tokens.
std::span<const Token> textTokens();
std::span<const Token> backgroundTokens();

// Spec 19 §1 pixel scale and §7 HiDPI: every pixel token multiplies by the DPI scale and then by
// the user font scale (0.8 … 1.6).
struct Metrics {
    float radiusSm = 3.0f, radiusMd = 5.0f, radiusLg = 8.0f;
    std::array<float, 6> space{4.0f, 8.0f, 12.0f, 16.0f, 24.0f, 32.0f};
    float bodyPx = 13.0f, secondaryPx = 12.0f, panelTitlePx = 15.0f, workspaceTitlePx = 20.0f;
    float editorPx = 13.0f, logPx = 12.0f, readoutPx = 16.0f;
    float border = 1.0f;

    // `space.1` … `space.6` of the spec table (1-based); out-of-range clamps.
    float spacing(int step) const;
    Metrics scaled(float factor) const;
};

class Theme {
public:
    // Loads `Assets/Lang/theme.json` (envelope kind "ui.theme"); falls back to the built-in table
    // only when the asset cannot be read, and then reports the failure.
    static Result<Theme> load(std::string_view palette = "dark");
    static Result<Theme> fromJson(const core::Json& themeData, std::string_view palette);
    // The spec 19 §1 tables compiled in, used when the asset is unavailable.
    static Theme fallback(bool dark = true);

    const Color& color(Token t) const { return colors_[static_cast<std::size_t>(t)]; }
    const Color& operator[](Token t) const { return color(t); }
    const Color& fidelityColor(data::FidelityClass c) const;
    // Spec 19 §1: qubit i uses qubit.{i % 8}; every wrap steps the lightness by 12 %.
    Color qubitColor(std::uint32_t qubit) const;
    const std::array<Color, 8>& qubitPalette() const { return qubits_; }

    const Metrics& metrics() const { return metrics_; }
    Metrics metricsAt(float dpiScale, float fontScale = 1.0f) const;
    bool dark() const { return dark_; }
    std::string_view name() const { return name_; }

    // The same tokens as `viz::VizTheme` so the state views draw in this theme (spec 21 §1).
    viz::VizTheme viz() const;

    // ---- accessibility (spec 19 §8)
    static double relativeLuminance(const Color& c);
    static double contrastRatio(const Color& a, const Color& b);
    double contrast(Token fg, Token bg) const { return contrastRatio(color(fg), color(bg)); }
    // Spec 19 §8 guarantees ≥ 4.5:1 for every text/background pair. The palette of §1 is pinned
    // (Okabe–Ito qubits, the status hues), and in the light palette `ok`/`warn` — drawn as badge
    // TEXT — sit at 4.10:1 / 3.64:1 on `bg.panel`. Text is therefore always emitted through this
    // helper: it returns `fg` when the pair already clears `minRatio`, else the same hue darkened
    // (light theme) or lightened (dark theme) until it does. Deterministic, 24 bisection steps.
    static Color readableText(const Color& fg, const Color& bg, double minRatio = 4.5);
    Color textOn(Token fg, Token bg, double minRatio = 4.5) const { return readableText(color(fg), color(bg), minRatio); }
    Color textOn(const Color& fg, Token bg, double minRatio = 4.5) const { return readableText(fg, color(bg), minRatio); }

    // "#RRGGBB" / "#RRGGBBAA".
    static Result<Color> parseHex(std::string_view hex);

private:
    std::array<Color, kTokenCount> colors_{};
    std::array<Color, 8> qubits_{};
    Metrics metrics_;
    std::string name_ = "dark";
    bool dark_ = true;
};

// ---------------------------------------------------------------- ImGui style (ThemeStyle.cpp)
// Spec 19 §9: replaces EVERY default ImGui colour, rounding and padding. Called once per theme or
// DPI change; `dpiScale` multiplies the pixel tokens (spec 19 §7).
// `uiScale` is the user font-size preference (spec 19 §7), NOT the display DPI: ImGui lays out
// in logical points and the platform scales those to device pixels.
void applyImGuiStyle(const Theme& theme, float uiScale = 1.0f);
// The §9 acceptance check: walks `ImGui::GetStyle()` and reports every entry that still holds an
// ImGui default instead of the theme's value. Empty result = the theme fully owns the style.
std::vector<std::string> imguiStyleMismatches(const Theme& theme, float uiScale = 1.0f);

} // namespace qlab::ui
