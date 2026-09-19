#pragma once
// Spec 17 §6 (RackUnit, rack detail pass) — the `front_panel` block of a rack-unit descriptor:
// the real front-panel features of the instrument class (screen, keypad, knobs, LEDs, connectors
// in their real layout, chassis handles, vent grooves, nameplate) parsed once so the generator
// and the tests read the same thing. Positions are fractions of the faceplate width / height
// (−0.5 … 0.5, x to the right, y up, seen from the front).
#include "Lab/Generators.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::lab {

enum class ConnectorType : std::uint8_t { Sma, N, Bnc, Iec, Dsub };
std::optional<ConnectorType>
connectorTypeFromName(std::string_view name); // "SMA", "N", "BNC", "IEC", "DSUB"
std::string_view connectorTypeName(ConnectorType t);

struct PanelConnector {
    ConnectorType type = ConnectorType::Sma;
    double x = 0.0, y = 0.0;
    std::string label; // "RF OUT", "CH 3", "LO" …
};
struct PanelKnob {
    double x = 0.0, y = 0.0;
    double r_m = 0.012;
};
struct PanelLed {
    double x = 0.0, y = 0.0;
    glm::vec4 colour{0.2f, 1.0f, 0.35f, 1.0f};
};
struct PanelRect { // screen
    double x = 0.0, y = 0.0, w = 0.4, h = 0.7;
};
struct PanelKeypad {
    double x = 0.0, y = 0.0;
    int rows = 1, cols = 1;
};
struct PanelVents {
    double x = 0.0, y = 0.0, w = 0.2, h = 0.6;
    int rows = 4;
};
struct PanelSlots { // a card cage: `count` plug-in cards each with `smaPerCard` SMA jacks and an
                    // LED
    int count = 8;
    double x0 = -0.35, x1 = 0.35;
    int smaPerCard = 4;
};

struct RackPanelSpec {
    int u = 1;
    std::string finish = "anodised_black"; // anodised_black | light_grey | blue_grey | off_white
    glm::vec4 faceColour{0.13f, 0.13f, 0.14f, 1.0f};
    bool handles = false;
    bool nameplate = true;
    std::optional<PanelRect> screen;
    std::vector<PanelKeypad> keypads;
    std::vector<PanelKnob> knobs;
    std::vector<PanelLed> leds;
    std::vector<PanelConnector> connectors; // explicit entries plus expanded `connector_rows`
    std::optional<PanelVents> vents;
    std::optional<PanelSlots> slots;

    // Every connector drawn on the panel: the listed ones plus the slot cards' SMA jacks.
    int connectorCount() const;
    int connectorCount(ConnectorType t) const;
};

// Reads `u` and `front_panel` from generator parameters. Without a `front_panel` block the legacy
// `front` kind gives a plain faceplate with two status LEDs and a nameplate.
RackPanelSpec parseRackPanel(const GenParams& p);

// Faceplate colour of a finish name (vertex colour; material "vertex").
glm::vec4 panelFinishColour(std::string_view finish);
// LED colour of a name: green (power), amber (status), red (fault), blue (lock/remote).
glm::vec4 ledColour(std::string_view name);

// Vertex colour of a connector type's body (SMA gold, N and BNC nickel, IEC socket black,
// D-sub shell nickel) and the number of body-coloured vertices one connector contributes at
// full detail — the oracle the tests count in a generated mesh.
glm::vec4 connectorBodyColour(ConnectorType t);
std::size_t connectorVertexCount(ConnectorType t);

// Nameplate position on a unit's faceplate in the unit's local frame (metres), where the
// renderer anchors the model label (SceneRenderer, spec 17 §3.3 amended).
glm::dvec3 rackNameplateLocal(int u, double depth_m);

} // namespace qlab::lab
