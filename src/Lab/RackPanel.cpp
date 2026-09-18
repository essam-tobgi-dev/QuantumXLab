// Spec 17 §6 (rack detail pass) — parsing the `front_panel` block of a RackUnit descriptor.
#include "Lab/RackPanel.hpp"
#include <algorithm>
#include <format>

namespace qlab::lab {

std::optional<ConnectorType> connectorTypeFromName(std::string_view name) {
    if (name == "SMA" || name == "sma") return ConnectorType::Sma;
    if (name == "N" || name == "n") return ConnectorType::N;
    if (name == "BNC" || name == "bnc") return ConnectorType::Bnc;
    if (name == "IEC" || name == "iec") return ConnectorType::Iec;
    if (name == "DSUB" || name == "dsub" || name == "D-sub") return ConnectorType::Dsub;
    return std::nullopt;
}

std::string_view connectorTypeName(ConnectorType t) {
    switch (t) {
    case ConnectorType::Sma: return "SMA";
    case ConnectorType::N: return "N";
    case ConnectorType::Bnc: return "BNC";
    case ConnectorType::Iec: return "IEC";
    case ConnectorType::Dsub: return "DSUB";
    }
    return "SMA";
}

glm::vec4 panelFinishColour(std::string_view finish) {
    if (finish == "light_grey") return {0.72f, 0.73f, 0.74f, 1.0f};
    if (finish == "blue_grey") return {0.36f, 0.42f, 0.50f, 1.0f};
    if (finish == "off_white") return {0.88f, 0.87f, 0.82f, 1.0f};
    return {0.13f, 0.13f, 0.14f, 1.0f}; // anodised black
}

glm::vec4 ledColour(std::string_view name) {
    if (name == "amber") return {1.0f, 0.62f, 0.10f, 1.0f};
    if (name == "red") return {1.0f, 0.12f, 0.10f, 1.0f};
    if (name == "blue") return {0.25f, 0.55f, 1.0f, 1.0f};
    return {0.20f, 1.0f, 0.35f, 1.0f}; // green
}

int RackPanelSpec::connectorCount() const {
    int n = static_cast<int>(connectors.size());
    if (slots) n += slots->count * slots->smaPerCard;
    return n;
}

int RackPanelSpec::connectorCount(ConnectorType t) const {
    int n = static_cast<int>(std::count_if(connectors.begin(), connectors.end(), [t](const PanelConnector& c) { return c.type == t; }));
    if (slots && t == ConnectorType::Sma) n += slots->count * slots->smaPerCard;
    return n;
}

namespace {
double num(const core::Json& j, const char* key, double fallback) {
    auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : fallback;
}
int integer(const core::Json& j, const char* key, int fallback) {
    auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<int>() : fallback;
}
std::string str(const core::Json& j, const char* key, std::string_view fallback = {}) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string(fallback);
}
} // namespace

RackPanelSpec parseRackPanel(const GenParams& p) {
    RackPanelSpec s;
    s.u = std::max(1, p.integer("u", 1));
    auto fp = p.json().find("front_panel");
    if (fp == p.json().end() || !fp->is_object()) { // legacy `front` kinds: a plain unit
        s.faceColour = panelFinishColour(s.finish);
        s.leds.push_back({-0.44, 0.25, ledColour("green")});
        s.leds.push_back({-0.42, 0.25, ledColour("amber")});
        return s;
    }
    const core::Json& j = *fp;
    s.finish = str(j, "finish", "anodised_black");
    s.faceColour = panelFinishColour(s.finish);
    s.handles = j.value("handles", false);
    s.nameplate = j.value("nameplate", true);
    if (auto it = j.find("screen"); it != j.end() && it->is_object())
        s.screen = PanelRect{num(*it, "x", 0.0), num(*it, "y", 0.0), num(*it, "w", 0.4), num(*it, "h", 0.7)};
    if (auto it = j.find("keypads"); it != j.end() && it->is_array())
        for (const auto& k : *it)
            if (k.is_object()) s.keypads.push_back({num(k, "x", 0.0), num(k, "y", 0.0), integer(k, "rows", 1), integer(k, "cols", 1)});
    if (auto it = j.find("knobs"); it != j.end() && it->is_array())
        for (const auto& k : *it)
            if (k.is_object()) s.knobs.push_back({num(k, "x", 0.0), num(k, "y", 0.0), 1e-3 * num(k, "r_mm", 12.0)});
    if (auto it = j.find("leds"); it != j.end() && it->is_array())
        for (const auto& k : *it)
            if (k.is_object()) s.leds.push_back({num(k, "x", 0.0), num(k, "y", 0.0), ledColour(str(k, "colour", "green"))});
    if (auto it = j.find("connectors"); it != j.end() && it->is_array())
        for (const auto& k : *it) {
            if (!k.is_object()) continue;
            auto type = connectorTypeFromName(str(k, "type", "SMA"));
            if (!type) continue;
            s.connectors.push_back({*type, num(k, "x", 0.0), num(k, "y", 0.0), str(k, "label")});
        }
    // A row of `count` identical connectors evenly spaced between x0 and x1 (a channel field).
    if (auto it = j.find("connector_rows"); it != j.end() && it->is_array())
        for (const auto& r : *it) {
            if (!r.is_object()) continue;
            auto type = connectorTypeFromName(str(r, "type", "SMA"));
            int count = std::max(1, integer(r, "count", 1));
            int rows = std::max(1, integer(r, "rows", 1));
            if (!type) continue;
            double x0 = num(r, "x0", -0.3), x1 = num(r, "x1", 0.3), y = num(r, "y", 0.0), dy = num(r, "dy", 0.35);
            std::string label = str(r, "label", "CH");
            int perRow = (count + rows - 1) / rows;
            for (int k = 0; k < count; ++k) {
                int row = k / perRow, col = k % perRow;
                double x = perRow > 1 ? x0 + (x1 - x0) * static_cast<double>(col) / static_cast<double>(perRow - 1) : 0.5 * (x0 + x1);
                double yy = y + dy * (0.5 * static_cast<double>(rows - 1) - static_cast<double>(row));
                s.connectors.push_back({*type, x, yy, std::format("{} {}", label, k + 1)});
            }
        }
    if (auto it = j.find("vents"); it != j.end() && it->is_object())
        s.vents = PanelVents{num(*it, "x", 0.0), num(*it, "y", 0.0), num(*it, "w", 0.2), num(*it, "h", 0.6), integer(*it, "rows", 4)};
    if (auto it = j.find("slots"); it != j.end() && it->is_object())
        s.slots = PanelSlots{std::max(1, integer(*it, "count", 8)), num(*it, "x0", -0.35), num(*it, "x1", 0.35), std::max(0, integer(*it, "sma", 4))};
    return s;
}

glm::dvec3 rackNameplateLocal(int u, double depth_m) {
    // Top-left corner of the faceplate, inside the rack ear, 4 mm in front of the panel face.
    return {-0.5 * kRackPanelWidth_m + 0.062, 0.5 * kRackUnit_m * std::max(1, u) - 0.010, 0.5 * depth_m - 0.015 + 0.004};
}

} // namespace qlab::lab
