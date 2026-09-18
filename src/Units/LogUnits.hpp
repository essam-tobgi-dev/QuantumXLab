#pragma once
// Spec 05 §4 — logarithmic units. Log = 1 marks them; they support only dB-chain arithmetic.
#include "Units/Dimension.hpp"
#include <cmath>

namespace qlab::units {

using PowerDbmDim = Dim<0, 0, 0, 0, 0, 0, 0, 0, 1>; // absolute power in dBm
using GainDbDim   = Dim<0, 0, 0, 0, 0, 0, 0, 0, 2>; // ratio in dB (distinct log marker)

struct PowerDbm {
    double v = 0.0; // dBm
    constexpr PowerDbm() = default;
    constexpr explicit PowerDbm(double dbm) : v(dbm) {}
    constexpr auto operator<=>(const PowerDbm&) const = default;
};
struct GainDb {
    double v = 0.0; // dB; attenuators are negative
    constexpr GainDb() = default;
    constexpr explicit GainDb(double db) : v(db) {}
    constexpr GainDb operator-() const { return GainDb(-v); }
    constexpr GainDb& operator+=(GainDb g) { v += g.v; return *this; }
    constexpr auto operator<=>(const GainDb&) const = default;
    constexpr double linear() const { return std::pow(10.0, v / 10.0); } // power ratio
};

constexpr GainDb operator+(GainDb a, GainDb b) { return GainDb(a.v + b.v); }
constexpr GainDb operator-(GainDb a, GainDb b) { return GainDb(a.v - b.v); }
constexpr GainDb operator*(GainDb a, double n) { return GainDb(a.v * n); }
constexpr GainDb operator*(double n, GainDb a) { return GainDb(a.v * n); }
constexpr PowerDbm operator+(PowerDbm p, GainDb g) { return PowerDbm(p.v + g.v); }
constexpr PowerDbm operator+(GainDb g, PowerDbm p) { return PowerDbm(p.v + g.v); }
constexpr PowerDbm operator-(PowerDbm p, GainDb g) { return PowerDbm(p.v - g.v); }
constexpr GainDb operator-(PowerDbm a, PowerDbm b) { return GainDb(a.v - b.v); }

// (5.1) P_dBm = 10 log10(P / 1 mW)
inline PowerDbm toDbm(Power p) { return PowerDbm(10.0 * std::log10(p.v / 1e-3)); }
inline Power fromDbm(PowerDbm p) { return Power(1e-3 * std::pow(10.0, p.v / 10.0)); }
inline Power toWatts(PowerDbm p) { return fromDbm(p); }
inline PowerDbm fromWatts(Power p) { return toDbm(p); }
// (5.2) G_dB = 10 log10(Pout/Pin)
inline GainDb powerRatioDb(double ratio) { return GainDb(10.0 * std::log10(ratio)); }
inline GainDb powerRatioDb(Power out, Power in) { return powerRatioDb(out.v / in.v); }
// Voltage ratio: 20 log10. Never crosses a module boundary (spec 05 §4).
inline double voltageDb(double ratio) { return 20.0 * std::log10(ratio); }
inline double fromVoltageDb(double db) { return std::pow(10.0, db / 20.0); }

namespace literals {
constexpr PowerDbm operator""_dBm(long double x) { return PowerDbm(static_cast<double>(x)); }
constexpr PowerDbm operator""_dBm(unsigned long long x) { return PowerDbm(static_cast<double>(x)); }
constexpr GainDb operator""_dB(long double x) { return GainDb(static_cast<double>(x)); }
constexpr GainDb operator""_dB(unsigned long long x) { return GainDb(static_cast<double>(x)); }
} // namespace literals

} // namespace qlab::units
