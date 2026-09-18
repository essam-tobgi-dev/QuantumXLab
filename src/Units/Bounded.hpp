#pragma once
// Spec 05 §3 — bounded dimensionless types: Probability, Fidelity, Population in [0,1]; Infidelity = 1 - F.
#include "Core/Error.hpp"
#include <compare>
#include <format>

namespace qlab::units {

namespace detail {
inline constexpr double kBoundEps = 1e-12;
template <class Self> struct Bounded01 {
    double v = 0.0;
    constexpr Bounded01() = default;
    // Validated construction: OutOfRange outside [0-eps, 1+eps]; clamps within the band.
    static Result<Self> make(double x) {
        if (!(x >= -kBoundEps && x <= 1.0 + kBoundEps))
            return fail(ErrorCode::OutOfRange,
                        std::format("{} value {} outside [0, 1]", Self::name(), x));
        Self s; s.v = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
        return s;
    }
    // Unchecked construction for values known to be in range (tests, kernels).
    static constexpr Self unsafe(double x) { Self s; s.v = x; return s; }
    constexpr double get() const { return v; }
    constexpr auto operator<=>(const Bounded01&) const = default;
};
} // namespace detail

struct Probability : detail::Bounded01<Probability> { static constexpr const char* name() { return "Probability"; } };
struct Fidelity    : detail::Bounded01<Fidelity>    { static constexpr const char* name() { return "Fidelity"; } };
struct Population  : detail::Bounded01<Population>  { static constexpr const char* name() { return "Population"; } };

// 1 - F stored separately to keep precision near F ≈ 1 (calibration files store this).
struct Infidelity {
    double v = 0.0;
    constexpr Infidelity() = default;
    constexpr explicit Infidelity(double r) : v(r) {}
    static Result<Infidelity> make(double r) {
        if (!(r >= -detail::kBoundEps && r <= 1.0 + detail::kBoundEps))
            return fail(ErrorCode::OutOfRange, std::format("Infidelity value {} outside [0, 1]", r));
        return Infidelity(r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r));
    }
    constexpr Fidelity fidelity() const { return Fidelity::unsafe(1.0 - v); }
    constexpr auto operator<=>(const Infidelity&) const = default;
};
constexpr Infidelity infidelityOf(Fidelity f) { return Infidelity(1.0 - f.v); }

} // namespace qlab::units
