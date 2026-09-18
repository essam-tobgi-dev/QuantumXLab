#pragma once
// Spec 10 §4 — control channels. A channel names one physical line of the device;
// `wiring.json` (spec 11 §9) maps it to a fridge line.
#include "Core/Error.hpp"
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace qlab::pulse {

enum class ChannelKind : std::uint8_t {
    Drive,      // d[i]      microwave drive of qubit i
    Control,    // u[i,j]    cross-resonance: drive i at the frequency of j
    Flux,       // f[i]      flux line of qubit or coupler i
    Measure,    // m[i]      readout tone
    Acquire,    // a[i]      digitiser acquisition window
    GlobalRaman,// g         ion global beam (no index)
    Raman,      // r[i]      ion individual addressing beam
    Bichromatic,// ms[i,j]   ion pair MS drive
    Detect,     // detect    ion fluorescence detection beam (no index)
    Pump,       // pump      ion optical pumping beam (no index)
};

std::string_view channelKindName(ChannelKind k);
// Number of indices the kind carries: 0 (g, detect, pump), 1 (d, f, m, a, r) or 2 (u, ms).
int channelKindArity(ChannelKind k);

// A channel identifier. `a` and `b` are qubit indices; unused slots are 0.
struct ChannelId {
    ChannelKind kind = ChannelKind::Drive;
    std::uint32_t a = 0;
    std::uint32_t b = 0;

    constexpr auto operator<=>(const ChannelId&) const = default;

    static ChannelId drive(std::uint32_t q) { return {ChannelKind::Drive, q, 0}; }
    static ChannelId control(std::uint32_t c, std::uint32_t t) { return {ChannelKind::Control, c, t}; }
    static ChannelId flux(std::uint32_t q) { return {ChannelKind::Flux, q, 0}; }
    static ChannelId measure(std::uint32_t q) { return {ChannelKind::Measure, q, 0}; }
    static ChannelId acquire(std::uint32_t q) { return {ChannelKind::Acquire, q, 0}; }
    static ChannelId globalRaman() { return {ChannelKind::GlobalRaman, 0, 0}; }
    static ChannelId raman(std::uint32_t q) { return {ChannelKind::Raman, q, 0}; }
    static ChannelId bichromatic(std::uint32_t i, std::uint32_t j) { return {ChannelKind::Bichromatic, i, j}; }
    static ChannelId detect() { return {ChannelKind::Detect, 0, 0}; }
    static ChannelId pump() { return {ChannelKind::Pump, 0, 0}; }

    // "d[0]", "u[0,1]", "g", "ms[0,1]", "detect", "pump"
    std::string toString() const;
    // True for channels that carry a pulse envelope (as opposed to acquisition windows).
    bool isDrivelike() const;
    // The qubit this channel acts on for scheduling purposes (the control qubit for u[c,t]).
    std::uint32_t primaryQubit() const { return a; }
};

Result<ChannelId> parseChannel(std::string_view s);

} // namespace qlab::pulse
