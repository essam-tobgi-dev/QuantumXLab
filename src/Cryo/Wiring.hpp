#pragma once
// Spec 11 §4, §9 — wiring lines as chains of elements from the RT bulkhead to the chip.
#include "Core/Error.hpp"
#include "Cryo/Stages.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::cryo {

enum class LineKind { XY, Flux, ReadoutIn, ReadoutOut, Pump, DC };
std::string_view lineKindName(LineKind k);
bool lineKindFromName(std::string_view s, LineKind& out);

enum class ElementKind {
    CoaxSegment,   // a cable run between this stage and the previous one
    Attenuator,    // A dB
    LowPassFilter, // fc Hz, order
    IrFilter,      // eccosorb: insertion loss dB
    Isolator,      // isolation dB, insertion loss dB, band
    Circulator,
    Amplifier,     // gain dB, noise temperature K, dissipation W (HEMT / RT amp)
    Preamp,        // jpa | twpa: gain dB, noise temperature K, pump dissipation W
    ThermalClamp,  // conductance W/K
    RcFilter,      // DC loom filter
    Bulkhead,
    Chip
};
std::string_view elementKindName(ElementKind k);

struct Element {
    ElementKind kind = ElementKind::ThermalClamp;
    Stage stage = Stage::RT;
    std::string id;         // unique within the line, e.g. "att_MXC"
    std::string coax;       // CoaxSegment: catalog id; length_m below
    double length_m = 0.0;
    double attenuation_dB = 0.0;  // Attenuator, IrFilter (insertion loss), Isolator (insertion loss)
    double isolation_dB = 0.0;    // Isolator/Circulator reverse isolation
    double cutoff_Hz = 0.0;       // filters
    int order = 5;                // filters
    double gain_dB = 0.0;         // amplifiers / preamps
    double noiseTemperature_K = 0.0;
    double dissipation_W = 0.0;   // amplifiers (HEMT default 12 mW), preamp pump
    double conductance_W_K = 0.0; // clamps
    std::string variant;          // Preamp: "jpa" | "twpa"
};

struct WiringLine {
    std::string id;
    std::string channel;       // "d[0]", "m[0..6]", "a[0..6]", "dc[0..11]"
    std::string chain;         // chain catalog id, e.g. "drive_std"
    LineKind kind = LineKind::XY;
    std::vector<Element> elements; // ordered from RT toward the chip (output lines: chip → RT)
    std::map<Stage, double> attenuationOverride_dB;
    std::optional<std::string> preamp; // "none" | "jpa" | "twpa"
    double hemtGain_dB = 38.0, hemtNoise_K = 2.5, hemtDissipation_W = 12e-3;

    double totalAttenuation_dB() const;
    double attenuationAt(Stage s) const;
    std::vector<const Element*> elementsAt(Stage s) const;
};

// Standard chains of spec 11 §4. `stageLengths_m` gives the coax run length ending at each
// stage (index by Stage; RT entry unused). Defaults 0.25/0.20/0.15/0.10/0.10 m (T08 §4.2).
struct ChainCatalog {
    static std::array<double, kStageCount> defaultLengths();
    static std::vector<std::string> ids();
    static bool has(std::string_view id);
    static Result<WiringLine> instantiate(std::string_view chainId, std::string lineId,
                                          std::string channel,
                                          const std::array<double, kStageCount>& lengths = defaultLengths());
};

struct Wiring {
    std::string device;
    std::string layout;
    std::vector<WiringLine> lines;
    std::array<double, kStageCount> stageLengths_m = ChainCatalog::defaultLengths();

    const WiringLine* find(std::string_view lineId) const;
    std::map<LineKind, int> countByKind() const;
};

// JSON (kind "wiring"; the older "qlab.wiring" is still read): {device, layout, lengths_m?, lines:[{id, channel, chain,
// attenuation_db?: {STAGE: dB}, preamp?, hemt?: {gain_db, noise_k, dissipation_mw}}]}
Result<Wiring> parseWiring(const std::string& text);
Result<Wiring> loadWiring(const std::filesystem::path& path);
std::string serializeWiring(const Wiring& w);

// Validation rules of spec 11 §5/§9: chain exists; attenuation in {0,3,6,10,20,30};
// no isolator/amplifier on input lines; no attenuator on output lines; every stage
// crossing has a clamp or attenuator (thermalization); channels served by exactly one line.
Status validateWiring(const Wiring& w);

} // namespace qlab::cryo
