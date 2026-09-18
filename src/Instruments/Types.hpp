#pragma once
// Spec 12 §1 — shared vocabulary of qlab::instr: error codes, identifiers, the instrument state
// machine, and the `Trace` record every instrument and probe returns.
//
// Deviation from the §1 sketch: `units::Unit` does not exist in the Units module, so a trace
// carries unit *symbols* that `units::UnitCatalog` resolves, exactly like `data::Trace2D`. The
// log-ratio symbols "dBc" and "dBc/Hz" are not in the catalog and are displayed verbatim.
#include "Core/Error.hpp"
#include "Core/StrongType.hpp"
#include "Data/Fidelity.hpp"
#include "Data/Series.hpp"
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace qlab::instr {

using data::FidelityClass;

// Error codes owned by this module (ErrorCode::Instr_ block, spec 04 §2).
namespace err {
inline constexpr ErrorCode UnknownSetting = ErrorCode::Instr_ + 1;
inline constexpr ErrorCode BadSettingType = ErrorCode::Instr_ + 2;  // wrong type, NaN, unknown enum value
inline constexpr ErrorCode UnknownChannel = ErrorCode::Instr_ + 3;
inline constexpr ErrorCode PoweredOff = ErrorCode::Instr_ + 4;      // acquire() while Off
inline constexpr ErrorCode Faulted = ErrorCode::Instr_ + 5;         // acquire() while in Fault; message = the fault
inline constexpr ErrorCode Busy = ErrorCode::Instr_ + 6;            // acquire() re-entered while Acquiring
inline constexpr ErrorCode NotBound = ErrorCode::Instr_ + 7;        // no RunView / Environment / schedule / route
inline constexpr ErrorCode NoSignal = ErrorCode::Instr_ + 8;        // the routed node delivers nothing
inline constexpr ErrorCode BadSchema = ErrorCode::Instr_ + 9;       // settings-schema JSON problem (names the field)
inline constexpr ErrorCode UnknownInstrument = ErrorCode::Instr_ + 10;
inline constexpr ErrorCode FitFailed = ErrorCode::Instr_ + 11;
inline constexpr ErrorCode BadRouting = ErrorCode::Instr_ + 12;
inline constexpr ErrorCode MemoryOverflow = ErrorCode::Instr_ + 13; // AWG / digitizer record exceeds memory
inline constexpr ErrorCode BadState = ErrorCode::Instr_ + 14;       // state-machine violation (arm while Off, …)
inline constexpr ErrorCode BadInput = ErrorCode::Instr_ + 15;       // inconsistent RunView / Environment content
inline constexpr ErrorCode Descriptor = ErrorCode::Instr_ + 16;     // component.json instrument block problem
} // namespace err

// Registry key plus the instance number among instruments of that kind ("sg_mw[2]").
struct InstrumentId {
    std::string kind;
    std::uint32_t index = 0;
    auto operator<=>(const InstrumentId&) const = default;
    std::string toString() const { return kind + "[" + std::to_string(index) + "]"; }
};

// Index into IInstrument::channels().
using ChannelId = core::Strong<std::uint32_t, struct InstrChannelTag>;

struct ChannelDesc {
    ChannelId id{0};
    std::string name;  // descriptor spelling with the index substituted: "ch[0].waveform", "s21"
    std::string xUnit, yUnit;
    FidelityClass cls = FidelityClass::Model;
    bool complexValued = false; // the trace carries y_im
    bool simulatorOnly = false; // spec 00 §6: probes
    std::string description;
};

// Spec 12 §1 state machine: Off → Idle (power on) → Armed (trigger source set) → Acquiring → Idle;
// Fault on a settings conflict, with a message.
enum class State : std::uint8_t { Off, Idle, Armed, Acquiring, Fault };
std::string_view stateName(State s);

// A setting value. `monostate` is what get() returns for an unknown key.
using SettingValue = std::variant<std::monostate, bool, std::int64_t, double, std::string>;
std::string settingToString(const SettingValue& v);
// Numeric view of a value (bool → 0/1); nullopt for text and monostate.
std::optional<double> settingNumber(const SettingValue& v);

// A labelled point on a trace: fit results, peaks, the playhead (spec 12 §1, §6).
struct Marker {
    double x = 0.0, y = 0.0;
    std::string label;
    double value = std::numeric_limits<double>::quiet_NaN(); // the quantity the marker reports
    double sigma = 0.0;                                     // its 1σ, 0 when exact
    std::string unit;
};

// Per-point 1σ of a Statistical trace (yIm empty unless the trace is complex).
struct Uncertainty {
    std::vector<double> y, yIm;
};

// When a trace was taken. No wall clock (DEVELOPMENT.md determinism): `labTimeS` is the lab clock
// of the Environment, `runTimeS` the schedule playhead, `sequence` the instrument's acquisition count.
struct Timestamp {
    double labTimeS = 0.0;
    double runTimeS = 0.0;
    std::uint64_t sequence = 0;
};

struct Trace {
    std::string instrument, channel;
    std::vector<double> x, y;
    std::vector<double> y_im; // empty unless complex
    std::string xUnit, yUnit;
    FidelityClass cls = FidelityClass::Model;
    Timestamp t;
    std::optional<Uncertainty> sigma;
    std::vector<Marker> markers;
    bool simulatorOnly = false; // spec 12 §12: set on every probe trace
    // Named per-point columns beyond (x, y): IQ-cloud labels "prepared"/"assigned", "line" of the
    // controller timeline. −1 encodes "unknown".
    std::map<std::string, std::vector<double>> aux;

    std::size_t size() const { return x.size(); }
    bool complexValued() const { return !y_im.empty(); }
    const Marker* marker(std::string_view label) const;
    // The plot record of spec 22 §2 (markers keep x and label).
    data::Trace2D toTrace2D() const;
};

// Spec 23 §7 — CSV with `#` provenance lines, then a names row and a units row; complex traces
// become `re, im` column pairs.
std::string traceToCsv(const Trace& t, std::string_view settingsComment = {});

// Conversions used across the models (T07 §3). Z0 = 50 Ω.
inline constexpr double kZ0 = 50.0;
double dbmFromWatts(double watts);
double wattsFromDbm(double dbm);
// Power of a tone whose complex-envelope amplitude is `peakVolts`: V²/(2 Z0).
double tonePowerWatts(double peakVolts);

} // namespace qlab::instr
