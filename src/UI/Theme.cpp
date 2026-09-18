// Spec 19 §1 — theme tokens (see Theme.hpp).
#include "UI/Theme.hpp"
#include "Data/Fidelity.hpp"
#include "Core/Paths.hpp"
#include "Viz/Math/Color.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace qlab::ui {
namespace {

struct NameEntry {
    Token token;
    std::string_view name;
};
constexpr std::array<NameEntry, kTokenCount> kNames{{{Token::BgBase, "bg.base"},
                                                     {Token::BgPanel, "bg.panel"},
                                                     {Token::BgRaised, "bg.raised"},
                                                     {Token::BgViewport, "bg.viewport"},
                                                     {Token::Border, "border"},
                                                     {Token::TextPrimary, "text.primary"},
                                                     {Token::TextSecondary, "text.secondary"},
                                                     {Token::TextDisabled, "text.disabled"},
                                                     {Token::Accent, "accent"},
                                                     {Token::AccentSoft, "accent.soft"},
                                                     {Token::Ok, "ok"},
                                                     {Token::Warn, "warn"},
                                                     {Token::Err, "err"},
                                                     {Token::SimOnly, "sim_only"},
                                                     {Token::ClassExact, "class.exact"},
                                                     {Token::ClassNumerical, "class.numerical"},
                                                     {Token::ClassStatistical, "class.statistical"},
                                                     {Token::ClassModel, "class.model"},
                                                     {Token::ClassIllustrative, "class.illustrative"}}};

// The tokens the UI renders text in: the three text tones plus every semantic colour that appears
// as a label (status line, fidelity badge, Simulator-only badge).
constexpr std::array<Token, 11> kTextTokens{Token::TextPrimary,      Token::TextSecondary,   Token::Accent,
                                            Token::Ok,               Token::Warn,            Token::Err,
                                            Token::SimOnly,          Token::ClassExact,      Token::ClassNumerical,
                                            Token::ClassStatistical, Token::ClassIllustrative};
constexpr std::array<Token, 3> kBgTokens{Token::BgBase, Token::BgPanel, Token::BgRaised};

// Spec 19 §1 dark / light tables, compiled in for the fallback.
constexpr std::array<std::string_view, kTokenCount> kDarkHex{
    "#0F1115", "#161A20", "#1E232B", "#0A0C10", "#2A3039", "#E6E9EF", "#9AA3B2", "#5F6875", "#4FA3FF", "#4FA3FF33",
    "#3DD68C", "#F5B841", "#FF5D5D", "#C084FC", "#3DD68C", "#4FA3FF", "#F5B841", "#FF9F43", "#9AA3B2"};
constexpr std::array<std::string_view, kTokenCount> kLightHex{
    "#F4F5F7", "#FFFFFF", "#EEF0F3", "#DDE1E6", "#C9CED6", "#1A1D22", "#5B6472", "#9AA3B2", "#1F6FEB", "#1F6FEB22",
    "#1B8F5A", "#B7791F", "#C62828", "#7C3AED", "#1B8F5A", "#1F6FEB", "#B7791F", "#C2410C", "#5B6472"};
// Okabe–Ito, colour-blind safe (spec 19 §1, §8); identical in both palettes.
constexpr std::array<std::string_view, 8> kOkabeIto{"#E69F00", "#56B4E9", "#009E73", "#F0E442",
                                                    "#0072B2", "#D55E00", "#CC79A7", "#999999"};

Color hexOrMagenta(std::string_view s) {
    return viz::math::colorFromHex(s).value_or(Color(1.0f, 0.0f, 1.0f, 1.0f));
}

// `{"value": 4, "unit": …}` is not used here; theme.json holds plain numbers.
float jsonFloat(const core::Json& j, std::string_view key, float fallback) {
    if (!j.is_object()) return fallback;
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return fallback;
    return it->get<float>();
}

} // namespace

// ---------------------------------------------------------------- names

std::string_view tokenName(Token t) {
    for (const NameEntry& e : kNames)
        if (e.token == t) return e.name;
    return "?";
}

std::optional<Token> tokenFromName(std::string_view name) {
    for (const NameEntry& e : kNames)
        if (e.name == name) return e.token;
    return std::nullopt;
}

std::span<const Token> textTokens() { return kTextTokens; }
std::span<const Token> backgroundTokens() { return kBgTokens; }

// ---------------------------------------------------------------- metrics

float Metrics::spacing(int step) const {
    const int i = std::clamp(step, 1, static_cast<int>(space.size()));
    return space[static_cast<std::size_t>(i - 1)];
}

Metrics Metrics::scaled(float factor) const {
    Metrics m = *this;
    const float f = std::max(0.1f, factor);
    m.radiusSm *= f;
    m.radiusMd *= f;
    m.radiusLg *= f;
    for (float& s : m.space) s *= f;
    m.bodyPx *= f;
    m.secondaryPx *= f;
    m.panelTitlePx *= f;
    m.workspaceTitlePx *= f;
    m.editorPx *= f;
    m.logPx *= f;
    m.readoutPx *= f;
    m.border *= f;
    return m;
}

// ---------------------------------------------------------------- colour maths

double Theme::relativeLuminance(const Color& c) { return viz::math::relativeLuminance(glm::vec3(c)); }

double Theme::contrastRatio(const Color& a, const Color& b) {
    return viz::math::contrastRatio(glm::vec3(a), glm::vec3(b));
}

Result<Color> Theme::parseHex(std::string_view hex) {
    if (auto c = viz::math::colorFromHex(hex)) return *c;
    return fail(err::BadToken, std::string("malformed colour '") + std::string(hex) + "' (expected #RRGGBB[AA])");
}

Color Theme::readableText(const Color& fg, const Color& bg, double minRatio) {
    if (contrastRatio(fg, bg) >= minRatio) return fg;
    // Move the hue toward whichever extreme is further from the background in luminance; the
    // contrast ratio is monotonic along that segment, so 24 bisection steps land within 1e-7.
    const glm::vec3 target = relativeLuminance(bg) > 0.18 ? glm::vec3(0.0f) : glm::vec3(1.0f);
    if (contrastRatio(Color(target, fg.a), bg) < minRatio) return Color(target, fg.a); // unreachable: best effort
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 24; ++i) {
        const double mid = 0.5 * (lo + hi);
        const Color c(viz::math::mixColor(glm::vec3(fg), target, static_cast<float>(mid)), fg.a);
        (contrastRatio(c, bg) >= minRatio ? hi : lo) = mid;
    }
    return Color(viz::math::mixColor(glm::vec3(fg), target, static_cast<float>(hi)), fg.a);
}

// ---------------------------------------------------------------- theme

const Color& Theme::fidelityColor(data::FidelityClass c) const {
    const auto i = static_cast<std::size_t>(Token::ClassExact) + static_cast<std::size_t>(c);
    return colors_[std::min(i, kTokenCount - 1)];
}

Color Theme::qubitColor(std::uint32_t qubit) const {
    Color c = qubits_[qubit % qubits_.size()];
    const float step = 0.12f * static_cast<float>(qubit / qubits_.size());
    if (step > 0.0f) {
        const glm::vec3 toward = dark_ ? glm::vec3(1.0f) : glm::vec3(0.0f);
        c = Color(viz::math::mixColor(glm::vec3(c), toward, std::min(step, 0.6f)), c.a);
    }
    return c;
}

Metrics Theme::metricsAt(float dpiScale, float fontScale) const {
    // Spec 19 §7: pixel tokens × DPI scale, the user font scale (0.8 … 1.6) multiplies again.
    return metrics_.scaled(std::max(0.25f, dpiScale) * std::clamp(fontScale, 0.8f, 1.6f));
}

viz::VizTheme Theme::viz() const {
    viz::VizTheme t = dark_ ? viz::VizTheme::fallbackDark() : viz::VizTheme::fallbackLight();
    t.dark = dark_;
    t.bgPanel = color(Token::BgPanel);
    t.bgRaised = color(Token::BgRaised);
    t.border = color(Token::Border);
    t.textPrimary = color(Token::TextPrimary);
    t.textSecondary = color(Token::TextSecondary);
    t.textDisabled = color(Token::TextDisabled);
    t.accent = color(Token::Accent);
    t.accentSoft = color(Token::AccentSoft);
    t.ok = color(Token::Ok);
    t.warn = color(Token::Warn);
    t.err = color(Token::Err);
    t.simOnly = color(Token::SimOnly);
    for (std::size_t i = 0; i < 5; ++i)
        t.fidelity[i] = colors_[static_cast<std::size_t>(Token::ClassExact) + i];
    for (std::size_t i = 0; i < qubits_.size(); ++i) t.qubits[i] = qubits_[i];
    t.radiusSm = metrics_.radiusSm;
    t.radiusMd = metrics_.radiusMd;
    t.space = metrics_.spacing(2);
    t.fontPx = metrics_.bodyPx;
    t.fontSmallPx = metrics_.secondaryPx;
    return t;
}

Theme Theme::fallback(bool dark) {
    Theme t;
    t.dark_ = dark;
    t.name_ = dark ? "dark" : "light";
    const auto& table = dark ? kDarkHex : kLightHex;
    for (std::size_t i = 0; i < kTokenCount; ++i) t.colors_[i] = hexOrMagenta(table[i]);
    for (std::size_t i = 0; i < kOkabeIto.size(); ++i) t.qubits_[i] = hexOrMagenta(kOkabeIto[i]);
    return t;
}

Result<Theme> Theme::fromJson(const core::Json& themeData, std::string_view palette) {
    const bool dark = palette != "light";
    Theme t = fallback(dark);
    t.name_ = std::string(palette);
    if (!themeData.is_object()) return fail(err::BadAsset, "theme: `data` is not an object");

    const auto palettes = themeData.find("palettes");
    if (palettes == themeData.end() || !palettes->is_object())
        return fail(err::BadAsset, "theme: missing `data.palettes`");
    const auto entry = palettes->find(palette);
    if (entry == palettes->end() || !entry->is_object())
        return fail(err::BadAsset, std::string("theme: missing `data.palettes.") + std::string(palette) + "`");
    for (const NameEntry& n : kNames) {
        const auto it = entry->find(n.name);
        if (it == entry->end()) continue; // a missing token keeps its §1 value
        if (!it->is_string())
            return fail(err::BadToken, std::string("theme: token `") + std::string(n.name) + "` is not a string");
        QXL_TRY_ASSIGN(const Color c, parseHex(it->get<std::string>()));
        t.colors_[static_cast<std::size_t>(n.token)] = c;
    }

    if (const auto q = themeData.find("qubit_colors"); q != themeData.end() && q->is_array()) {
        for (std::size_t i = 0; i < t.qubits_.size() && i < q->size(); ++i) {
            if (!(*q)[i].is_string())
                return fail(err::BadToken, "theme: `qubit_colors` entry is not a string");
            QXL_TRY_ASSIGN(const Color c, parseHex((*q)[i].get<std::string>()));
            t.qubits_[i] = c;
        }
    }
    if (const auto r = themeData.find("radius_px"); r != themeData.end()) {
        t.metrics_.radiusSm = jsonFloat(*r, "sm", t.metrics_.radiusSm);
        t.metrics_.radiusMd = jsonFloat(*r, "md", t.metrics_.radiusMd);
        t.metrics_.radiusLg = jsonFloat(*r, "lg", t.metrics_.radiusLg);
    }
    if (const auto s = themeData.find("space_px"); s != themeData.end() && s->is_array()) {
        for (std::size_t i = 0; i < t.metrics_.space.size() && i < s->size(); ++i)
            if ((*s)[i].is_number()) t.metrics_.space[i] = (*s)[i].get<float>();
    }
    if (const auto ty = themeData.find("typography"); ty != themeData.end() && ty->is_object()) {
        if (const auto u = ty->find("ui"); u != ty->end()) {
            t.metrics_.bodyPx = jsonFloat(*u, "body_px", t.metrics_.bodyPx);
            t.metrics_.secondaryPx = jsonFloat(*u, "secondary_px", t.metrics_.secondaryPx);
            t.metrics_.panelTitlePx = jsonFloat(*u, "panel_title_px", t.metrics_.panelTitlePx);
            t.metrics_.workspaceTitlePx = jsonFloat(*u, "workspace_title_px", t.metrics_.workspaceTitlePx);
        }
        if (const auto c = ty->find("code"); c != ty->end()) {
            t.metrics_.editorPx = jsonFloat(*c, "editor_px", t.metrics_.editorPx);
            t.metrics_.logPx = jsonFloat(*c, "log_px", t.metrics_.logPx);
            t.metrics_.readoutPx = jsonFloat(*c, "readout_px", t.metrics_.readoutPx);
        }
    }
    return t;
}

Result<Theme> Theme::load(std::string_view palette) {
    const std::filesystem::path path = core::assetDir() / "Lang" / "theme.json";
    QXL_TRY_ASSIGN(const core::Envelope env, core::JsonEnvelope::load(path, "ui.theme"));
    return fromJson(env.data, palette);
}

} // namespace qlab::ui
