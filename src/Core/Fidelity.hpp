#pragma once
// Fidelity class of a displayed quantity (spec 00 §5). This is project-wide vocabulary — ten
// modules from Layer 1 upward record it alongside every number they publish — so it lives in
// Core (Layer 0) where every layer may depend on it. `qlab::data::FidelityClass` is an alias of
// this type, so either spelling names the same enum.
#include <string_view>

namespace qlab {

enum class FidelityClass { Exact, Numerical, Statistical, Model, Illustrative };

constexpr std::string_view fidelityName(FidelityClass c) {
    switch (c) {
    case FidelityClass::Exact:
        return "Exact";
    case FidelityClass::Numerical:
        return "Numerical";
    case FidelityClass::Statistical:
        return "Statistical";
    case FidelityClass::Model:
        return "Model";
    case FidelityClass::Illustrative:
        return "Illustrative";
    }
    return "?";
}

// A quantity takes the weakest class of its inputs (spec 00 §5):
// Exact > Numerical > Statistical > Model > Illustrative.
constexpr FidelityClass weakest(FidelityClass a, FidelityClass b) {
    return a > b ? a : b;
}

} // namespace qlab
