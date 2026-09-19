// Spec 19 §1 — theme tokens for the views (see Theme.hpp).
#include "Viz/Theme.hpp"
#include "Core/Paths.hpp"
#include "Viz/Math/Color.hpp"
#include <string>

namespace qlab::viz {
namespace {

glm::vec4 hex(std::string_view s) {
    return math::colorFromHex(s).value_or(glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));
}

void setQubitPalette(VizTheme& t) {
    // Okabe–Ito, colour-blind safe (spec 19 §1, §8).
    constexpr std::array<std::string_view, 8> kOkabeIto{"#E69F00", "#56B4E9", "#009E73", "#F0E442",
                                                        "#0072B2", "#D55E00", "#CC79A7", "#999999"};
    for (std::size_t i = 0; i < kOkabeIto.size(); ++i)
        t.qubits[i] = hex(kOkabeIto[i]);
}

struct TokenRef {
    std::string_view name;
    glm::vec4 VizTheme::* field;
};
constexpr std::array<TokenRef, 12> kTokens{{{"bg.panel", &VizTheme::bgPanel},
                                            {"bg.raised", &VizTheme::bgRaised},
                                            {"border", &VizTheme::border},
                                            {"text.primary", &VizTheme::textPrimary},
                                            {"text.secondary", &VizTheme::textSecondary},
                                            {"text.disabled", &VizTheme::textDisabled},
                                            {"accent", &VizTheme::accent},
                                            {"accent.soft", &VizTheme::accentSoft},
                                            {"ok", &VizTheme::ok},
                                            {"warn", &VizTheme::warn},
                                            {"err", &VizTheme::err},
                                            {"sim_only", &VizTheme::simOnly}}};
constexpr std::array<std::string_view, 5> kClassTokens{
    "class.exact", "class.numerical", "class.statistical", "class.model", "class.illustrative"};

} // namespace

glm::vec4 VizTheme::qubitColor(std::uint32_t qubit) const {
    glm::vec4 c = qubits[qubit % qubits.size()];
    const float step = 0.12f * static_cast<float>(qubit / qubits.size());
    if (step > 0.0f) {
        const glm::vec3 toward =
            dark ? glm::vec3(1.0f) : glm::vec3(0.0f); // stay readable on the panel
        c = glm::vec4(math::mixColor(glm::vec3(c), toward, std::min(step, 0.6f)), c.a);
    }
    return c;
}

glm::vec4 VizTheme::neutral() const {
    return glm::vec4(math::mixColor(glm::vec3(textSecondary), glm::vec3(bgPanel), 0.35f), 1.0f);
}

VizTheme VizTheme::fallbackDark() {
    VizTheme t;
    t.dark = true;
    t.bgPanel = hex("#161A20");
    t.bgRaised = hex("#1E232B");
    t.border = hex("#2A3039");
    t.textPrimary = hex("#E6E9EF");
    t.textSecondary = hex("#9AA3B2");
    t.textDisabled = hex("#5F6875");
    t.accent = hex("#4FA3FF");
    t.accentSoft = hex("#4FA3FF33");
    t.ok = hex("#3DD68C");
    t.warn = hex("#F5B841");
    t.err = hex("#FF5D5D");
    t.simOnly = hex("#C084FC");
    t.fidelity = {hex("#3DD68C"), hex("#4FA3FF"), hex("#F5B841"), hex("#FF9F43"), hex("#9AA3B2")};
    setQubitPalette(t);
    return t;
}

VizTheme VizTheme::fallbackLight() {
    VizTheme t;
    t.dark = false;
    t.bgPanel = hex("#FFFFFF");
    t.bgRaised = hex("#EEF0F3");
    t.border = hex("#C9CED6");
    t.textPrimary = hex("#1A1D22");
    t.textSecondary = hex("#5B6472");
    t.textDisabled = hex("#9AA3B2");
    t.accent = hex("#1F6FEB");
    t.accentSoft = hex("#1F6FEB22");
    t.ok = hex("#1B8F5A");
    t.warn = hex("#B7791F");
    t.err = hex("#C62828");
    t.simOnly = hex("#7C3AED");
    t.fidelity = {hex("#1B8F5A"), hex("#1F6FEB"), hex("#B7791F"), hex("#C2410C"), hex("#5B6472")};
    setQubitPalette(t);
    return t;
}

Result<VizTheme> VizTheme::fromJson(const core::Json& data, std::string_view palette) {
    VizTheme t = palette == "light" ? fallbackLight() : fallbackDark();
    if (!data.is_object())
        return fail(ErrorCode::Parse, "theme: data is not an object");
    const auto palettes = data.find("palettes");
    if (palettes == data.end() || !palettes->is_object() ||
        !palettes->contains(std::string(palette)))
        return fail(ErrorCode::NotFound,
                    "theme: missing field data.palettes." + std::string(palette));
    const core::Json& tokens = (*palettes)[std::string(palette)];
    auto read = [&](std::string_view name, glm::vec4& into) -> Status {
        const auto it = tokens.find(std::string(name));
        if (it == tokens.end())
            return {}; // unknown or absent tokens keep the fallback (DEVELOPMENT.md)
        if (!it->is_string())
            return fail(ErrorCode::Parse, "theme: token " + std::string(name) + " is not a string");
        const auto c = math::colorFromHex(it->get<std::string>());
        if (!c)
            return fail(ErrorCode::Parse,
                        "theme: token " + std::string(name) + " is not #RRGGBB[AA]");
        into = *c;
        return {};
    };
    for (const TokenRef& ref : kTokens)
        QXL_TRY(read(ref.name, t.*(ref.field)));
    for (std::size_t k = 0; k < kClassTokens.size(); ++k)
        QXL_TRY(read(kClassTokens[k], t.fidelity[k]));
    if (const auto q = data.find("qubit_colors"); q != data.end() && q->is_array())
        for (std::size_t i = 0; i < q->size() && i < t.qubits.size(); ++i) {
            if (!(*q)[i].is_string())
                return fail(ErrorCode::Parse,
                            "theme: qubit_colors[" + std::to_string(i) + "] is not a string");
            const auto c = math::colorFromHex((*q)[i].get<std::string>());
            if (!c)
                return fail(ErrorCode::Parse,
                            "theme: qubit_colors[" + std::to_string(i) + "] is not #RRGGBB");
            t.qubits[i] = *c;
        }
    if (const auto r = data.find("radius_px"); r != data.end() && r->is_object()) {
        t.radiusSm = r->value("sm", t.radiusSm);
        t.radiusMd = r->value("md", t.radiusMd);
    }
    return t;
}

Result<VizTheme> VizTheme::load(std::string_view palette) {
    QXL_TRY_ASSIGN(auto env,
                   core::JsonEnvelope::load(core::assetDir() / "Lang" / "theme.json", "ui.theme"));
    return fromJson(env.data, palette);
}

} // namespace qlab::viz
