#pragma once
// Spec 17 §5 — dotted binding paths, resolved values and placeholder substitution.
#include "Data/Fidelity.hpp"
#include "Lab/Types.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace qlab::lab {

// The six root namespaces of spec 17 §5.
enum class BindingRoot : std::uint8_t { Cryo, Wiring, Device, Instr, Static, Run };
inline constexpr int kBindingRootCount = 6;
std::string_view bindingRootName(BindingRoot r);
std::optional<BindingRoot> bindingRootFromName(std::string_view s);

// A binding bound to one scene node: root + the path after "root." with every `$name`
// placeholder already substituted from the node's InstanceParams.
struct Binding {
    BindingRoot root = BindingRoot::Static;
    std::string path;  // "line[3].attn[0].P_diss"
    std::string field; // spec_sheet row the binding was declared on
    std::string unit;
    data::FidelityClass cls = data::FidelityClass::Model;
    bool simulatorOnly = false;

    std::string fullPath() const; // "wiring.line[3].attn[0].P_diss"
};

// A resolved live value: a number or a short string (e.g. a GHS state name). Vector-valued
// bindings such as `.bloch` are published by providers as consecutive scalar paths
// (`bloch[0]`, `bloch[1]`, `bloch[2]`), spec 17 §5.
struct BindingValue {
    std::variant<double, std::string> value{std::numeric_limits<double>::quiet_NaN()};
    std::string unit;
    data::FidelityClass cls = data::FidelityClass::Model;
    bool simulatorOnly = false;

    static BindingValue number(double v, std::string unit = {},
                               data::FidelityClass cls = data::FidelityClass::Model) {
        return BindingValue{v, std::move(unit), cls, false};
    }
    static BindingValue text(std::string s, data::FidelityClass cls = data::FidelityClass::Model) {
        return BindingValue{std::move(s), {}, cls, false};
    }
    bool isNumber() const { return std::holds_alternative<double>(value); }
    // NaN reads as "not available" (spec 17 §5: a disabled probe reads NaN and its overlay hides).
    bool available() const { return !isNumber() || std::isfinite(std::get<double>(value)); }
    double asNumber(double fallback = std::numeric_limits<double>::quiet_NaN()) const {
        return isNumber() ? std::get<double>(value) : fallback;
    }
    const std::string* asText() const { return std::get_if<std::string>(&value); }
};

// Substitutes every `$name` in `path` from `p`. Returns nullopt when a placeholder has no (or an
// empty) instance value: the row then displays "—" (spec 17 §5).
std::optional<std::string> substitutePath(std::string_view path, const InstanceParams& p);

// Splits "root.rest" into (BindingRoot, rest). Fails with kErrBinding for an unknown root or a
// path without a dot.
Result<std::pair<BindingRoot, std::string>> splitBindingRoot(std::string_view fullPath);

// Spec 00 §6: probe-only quantities (Bloch vector, purity, state vector) are Simulator-only.
bool isSimulatorOnlyPath(std::string_view path);

// Inspector text of a value: "—" when absent/NaN, otherwise 4 significant digits and the unit
// (the provider's unit, or `unitFallback` from the spec row).
std::string formatBindingValue(const std::optional<BindingValue>& v, std::string_view unitFallback = {});

} // namespace qlab::lab
