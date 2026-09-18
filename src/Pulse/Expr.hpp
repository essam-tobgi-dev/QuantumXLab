#pragma once
// Spec 10 §6 — the tiny expression language used by `pulses.json` values:
// numbers, gate parameters (`theta`), `pi`, and `cal.*` / `device.*` references,
// combined with + - * / and parentheses. Evaluated at load time (spec 10 §6).
#include "Core/Error.hpp"
#include "Core/Json.hpp"
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace qlab::hw { struct Device; struct Calibration; }

namespace qlab::pulse {

// Resolves a dotted path such as "cal.qubits.0.f01_ghz" or "device.ion.f_qubit_ghz" to a number
// in the unit the path names (`_ghz` → GHz, `_ns` → ns, …).
using PathResolver = std::function<Result<double>(std::string_view path)>;
using ParamMap = std::map<std::string, double, std::less<>>;

// Identifiers are parameter names, `pi`, `e`, or dotted `cal.` / `device.` paths. Only a dotted
// path may contain '-' (edge keys such as `cal.edges.0-1.duration_ns`); elsewhere '-' subtracts.
Result<double> evalExpression(std::string_view text, const ParamMap& params, const PathResolver& paths);
// Convenience for JSON fields that may be a number or an expression string.
Result<double> evalJsonValue(const core::Json& j, const ParamMap& params, const PathResolver& paths);
// Resolver over raw calibration/device JSON trees (`[value, sigma, source]` triples → value).
// The trees are captured by reference and must outlive the resolver.
PathResolver makeJsonResolver(const core::Json& calibration, const core::Json& device);
// Resolver over the typed calibration (spec 10 §6: `cal.*` values follow a recalibration without
// editing pulses.json). Units follow the calibration.json field names of spec 09 §3. The objects
// are captured by reference and must outlive the resolver.
PathResolver makeCalibrationResolver(const hw::Device& device, const hw::Calibration& calibration);

} // namespace qlab::pulse
