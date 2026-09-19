// Spec 16 §1–§6 — names of the module's enumerations and the Wilson score interval (spec 16 §6).
#include "QEC/Types.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::qec {

std::string_view familyName(CodeFamily f) {
    switch (f) {
    case CodeFamily::Repetition:
        return "Repetition";
    case CodeFamily::Shor:
        return "Shor";
    case CodeFamily::Steane:
        return "Steane";
    case CodeFamily::FiveQubit:
        return "FiveQubit";
    case CodeFamily::SurfaceRotated:
        return "SurfaceRotated";
    case CodeFamily::Other:
        return "Other";
    }
    return "Other";
}

std::string_view checkTypeName(CheckType t) {
    switch (t) {
    case CheckType::X:
        return "X";
    case CheckType::Z:
        return "Z";
    case CheckType::Mixed:
        return "M";
    }
    return "M";
}

std::string_view basisName(LogicalBasis b) {
    return b == LogicalBasis::Z ? "Z" : "X";
}

std::string_view noiseSettingName(NoiseSetting s) {
    switch (s) {
    case NoiseSetting::CodeCapacity:
        return "code_capacity";
    case NoiseSetting::Phenomenological:
        return "phenomenological";
    case NoiseSetting::CircuitLevel:
        return "circuit_level";
    }
    return "circuit_level";
}

std::optional<NoiseSetting> noiseSettingFromName(std::string_view name) {
    for (NoiseSetting s :
         {NoiseSetting::CodeCapacity, NoiseSetting::Phenomenological, NoiseSetting::CircuitLevel})
        if (noiseSettingName(s) == name)
            return s;
    return std::nullopt;
}

Interval wilsonInterval(std::uint64_t k, std::uint64_t n, double z) {
    if (n == 0)
        return {0.0, 1.0, 0.5};
    const double nn = static_cast<double>(n), p = static_cast<double>(k) / nn;
    const double z2 = z * z;
    const double denom = 1.0 + z2 / nn;
    const double center = (p + z2 / (2.0 * nn)) / denom;
    const double half = z / denom * std::sqrt(p * (1.0 - p) / nn + z2 / (4.0 * nn * nn));
    return {std::max(0.0, center - half), std::min(1.0, center + half), center};
}

} // namespace qlab::qec
