#pragma once
// Spec 12 §1, §14 — the settings schema of an instrument: a JSON-schema subset (name, type, unit,
// range, step, enum) that validates every `set`, clamps out-of-range values and says so, and
// round-trips through JSON (spec 12 §15, spec 23). The descriptor's `instrument.settings_schema`
// (e.g. "instr/vna.schema.json") is the schema `id`.
#include "Core/Json.hpp"
#include "Instruments/Types.hpp"
#include <limits>
#include <string>
#include <vector>

namespace qlab::instr {

enum class SettingType : std::uint8_t { Bool, Int, Real, Enum, Text };
std::string_view settingTypeName(SettingType t);

struct SettingSpec {
    std::string key;
    SettingType type = SettingType::Real;
    std::string unit; // catalog symbol of the stored value ("Hz", "dBm", "V", …); empty = dimensionless
    double min = -std::numeric_limits<double>::infinity();
    double max = std::numeric_limits<double>::infinity();
    double step = 0.0;                // resolution the value is rounded to (silent); 0 = continuous
    std::vector<double> allowed;      // Real/Int with discrete values (AWG sample rates); snapped and reported
    std::vector<std::string> options; // Enum
    SettingValue defaultValue;
    std::string description;
    bool readOnly = false;            // a spec-sheet constant shown in the panel (resolution_bits)

    static SettingSpec real(std::string key, std::string unit, double min, double max, double step, double def,
                            std::string description);
    static SettingSpec integer(std::string key, std::string unit, std::int64_t min, std::int64_t max,
                               std::int64_t def, std::string description);
    static SettingSpec boolean(std::string key, bool def, std::string description);
    static SettingSpec choice(std::string key, std::vector<std::string> options, std::string def,
                              std::string description);
    static SettingSpec text(std::string key, std::string def, std::string description);
    static SettingSpec discrete(std::string key, std::string unit, std::vector<double> allowed, double def,
                                std::string description);
    SettingSpec& constant() { readOnly = true; return *this; }

    bool operator==(const SettingSpec&) const = default;
};

// Result of validating one value against its spec.
struct Coerced {
    SettingValue value;   // what the instrument stores
    bool clamped = false; // the request was outside the schema and was pulled in (reported)
    std::string message;  // "frequency 2.5e+10 Hz above the maximum 2e+10 Hz: clamped"
};

struct SettingSchema {
    std::string id;         // "instr/sg_mw.schema.json"
    std::string instrument; // registry kind
    std::vector<SettingSpec> settings;

    const SettingSpec* find(std::string_view key) const;
    // Type check, range clamp, discrete snap, step rounding. Errors: UnknownSetting, BadSettingType
    // (wrong type, NaN/inf, enum value not listed, write to a read-only setting unless
    // `allowReadOnly`, which tools writing derived values use).
    Result<Coerced> coerce(std::string_view key, const SettingValue& v, bool allowReadOnly = false) const;

    core::Json toJson() const;
    static Result<SettingSchema> fromJson(const core::Json& j);
    bool operator==(const SettingSchema&) const = default;
};

// Spec 12 §1: every instrument has `refresh_hz` (default 10), the live-mode acquisition rate.
SettingSpec refreshRateSetting();

core::Json settingValueToJson(const SettingValue& v);
SettingValue settingValueFromJson(const core::Json& j);

// One clamp the instrument performed ("a physical instrument beeps; the panel shows the clamp").
// Kept in the instrument's report log and posted on the event bus when one is bound.
struct SettingReport {
    InstrumentId instrument;
    std::string key;
    SettingValue requested, applied;
    std::string message;
    std::uint64_t sequence = 0;
};

} // namespace qlab::instr
