#pragma once
// Spec 19 §5.1 — every numeric the UI shows carries its unit, with SI-prefix auto-selection
// (12.4 GHz, 35 mK, 320 ns), and every numeric the user types is parsed through the same catalog so
// that "5 us", "5000 ns" and "0.000005 s" all reach the field as 5e-6 s. This is the whole numeric
// presentation layer of the UI; it is headless and is what the widget tests exercise.
#include "Core/Error.hpp"
#include "Data/Fidelity.hpp"
#include "Units/UnitCatalog.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace qlab::ui::format {

// A displayed quantity: the SI value, the unit symbol it is *declared* in (may be any symbol of the
// dimension, "" for dimensionless) and how precise it is.
struct Quantity {
    double si = 0.0;
    std::string unit;                                        // catalog symbol, e.g. "Hz", "K", "s"
    data::FidelityClass cls = data::FidelityClass::Model;
    bool simulatorOnly = false;                              // spec 19 §5.5
};

// "12.4 GHz" — value with the auto-selected SI prefix and a space before the symbol. An empty or
// unknown unit formats the bare number; a non-finite value is "—" (spec 17 §5).
std::string value(double si, std::string_view unit, int digits = 4,
                  units::FormatContext ctx = units::FormatContext::Default);
inline std::string value(const Quantity& q, int digits = 4) { return value(q.si, q.unit, digits); }

// Just the number, 4 significant figures by default, with the same prefix choice as `value`.
std::string number(double v, int digits = 4);
// The unit symbol `value` would use for this magnitude ("GHz", "mK", "ns"), "" when there is none.
std::string prefixedUnit(double si, std::string_view unit);

// Value ± 1σ in one string, both in the same prefixed unit: "4.812 ± 0.003 GHz".
std::string withSigma(double si, double sigma, std::string_view unit, int digits = 4);
// A count or an integer with thousands separators ("1 048 576" with a thin space).
std::string integer(std::uint64_t v);
// Seconds as a human duration for the run/estimate panels: "3.2 ms", "1 min 12 s", "2 h 05 min".
std::string duration(double seconds);
// Bytes as "1.5 GiB".
std::string bytes(double count);
// A percentage with the given decimals: "99.73 %".
std::string percent(double fraction, int decimals = 2);

// Spec 19 §5.1: accepts any registered unit of the same dimension. `defaultUnit` is used for a bare
// number. The error names the offending text and the expected dimension.
Result<double> parse(std::string_view text, std::string_view defaultUnit);

// Spec 19 §5.2 drag-to-edit: plain drag is coarse, Shift is ×0.1, Alt is ×10.
struct DragModifiers {
    bool shift = false;
    bool alt = false;
};
double dragScale(DragModifiers mods);
// New value after dragging `pixels` on a field whose coarse step is `step` per pixel, clamped to
// [lo, hi] (either may be infinite).
double applyDrag(double current, double pixels, double step, DragModifiers mods, double lo, double hi);

// Spec 19 §5.1 default format context for a unit symbol: gate durations in ns, cryogenic
// temperatures in mK, RF powers in dBm.
units::FormatContext contextFor(std::string_view unit);

} // namespace qlab::ui::format
