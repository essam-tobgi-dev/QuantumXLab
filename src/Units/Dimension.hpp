#pragma once
// Spec 05 §1–2 — compile-time dimensional analysis. A Q always stores the SI-coherent value.
#include <cmath>
#include <compare>
#include <numbers>
#include <type_traits>

namespace qlab::units {

// Exponents of the seven SI base dimensions plus two project pseudo-dimensions:
// time, length, mass, current, temperature, amount, luminous, angle (rad), log-marker.
template <int T, int L, int M, int I, int Th, int N, int J, int Ang = 0, int Log = 0>
struct Dim {
    static constexpr int t = T, l = L, m = M, i = I, th = Th, n = N, j = J, ang = Ang, log = Log;
};

template <class A, class B>
using DimMul = Dim<A::t + B::t, A::l + B::l, A::m + B::m, A::i + B::i, A::th + B::th, A::n + B::n,
                   A::j + B::j, A::ang + B::ang, A::log + B::log>;
template <class A, class B>
using DimDiv = Dim<A::t - B::t, A::l - B::l, A::m - B::m, A::i - B::i, A::th - B::th, A::n - B::n,
                   A::j - B::j, A::ang - B::ang, A::log - B::log>;
template <class A, int P>
using DimPow = Dim<A::t * P, A::l * P, A::m * P, A::i * P, A::th * P, A::n * P, A::j * P,
                   A::ang * P, A::log * P>;
template <class A>
using DimSqrt = Dim<A::t / 2, A::l / 2, A::m / 2, A::i / 2, A::th / 2, A::n / 2, A::j / 2,
                    A::ang / 2, A::log / 2>;

template <class A>
inline constexpr bool isDimensionless =
    A::t == 0 && A::l == 0 && A::m == 0 && A::i == 0 && A::th == 0 && A::n == 0 && A::j == 0 &&
    A::ang == 0 && A::log == 0;
template <class A>
inline constexpr bool isEvenDim = A::t % 2 == 0 && A::l % 2 == 0 && A::m % 2 == 0 &&
                                  A::i % 2 == 0 && A::th % 2 == 0 && A::n % 2 == 0 &&
                                  A::j % 2 == 0 && A::ang % 2 == 0 && A::log % 2 == 0;

using NoDim = Dim<0, 0, 0, 0, 0, 0, 0>;

template <class D> struct Q {
    using dim = D;
    double v; // SI-coherent value (s, m, kg, A, K, mol, cd, rad)

    constexpr Q() : v(0.0) {}
    constexpr explicit Q(double val) : v(val) {}
    constexpr double si() const { return v; }

    // Dimensionless quantities convert implicitly to double (spec 05 §2).
    template <class DD = D, class = std::enable_if_t<isDimensionless<DD>>>
    constexpr operator double() const { return v; }

    constexpr Q operator-() const { return Q(-v); }
    constexpr Q& operator+=(Q o) { v += o.v; return *this; }
    constexpr Q& operator-=(Q o) { v -= o.v; return *this; }
    constexpr Q& operator*=(double s) { v *= s; return *this; }
    constexpr Q& operator/=(double s) { v /= s; return *this; }
    constexpr auto operator<=>(const Q&) const = default;
};

template <class D> constexpr Q<D> operator+(Q<D> a, Q<D> b) { return Q<D>(a.v + b.v); }
template <class D> constexpr Q<D> operator-(Q<D> a, Q<D> b) { return Q<D>(a.v - b.v); }
template <class D> constexpr Q<D> operator*(Q<D> a, double s) { return Q<D>(a.v * s); }
template <class D> constexpr Q<D> operator*(double s, Q<D> a) { return Q<D>(a.v * s); }
template <class D> constexpr Q<D> operator/(Q<D> a, double s) { return Q<D>(a.v / s); }
template <class D> constexpr Q<DimDiv<NoDim, D>> operator/(double s, Q<D> a) {
    return Q<DimDiv<NoDim, D>>(s / a.v);
}
template <class A, class B> constexpr Q<DimMul<A, B>> operator*(Q<A> a, Q<B> b) {
    static_assert(A::log == 0 && B::log == 0, "logarithmic units cannot be multiplied");
    return Q<DimMul<A, B>>(a.v * b.v);
}
template <class A, class B> constexpr Q<DimDiv<A, B>> operator/(Q<A> a, Q<B> b) {
    static_assert(A::log == 0 && B::log == 0, "logarithmic units cannot be divided");
    return Q<DimDiv<A, B>>(a.v / b.v);
}
template <int P, class D> constexpr Q<DimPow<D, P>> pow(Q<D> a) {
    double r = 1.0;
    if constexpr (P >= 0) { for (int k = 0; k < P; ++k) r *= a.v; }
    else { for (int k = 0; k < -P; ++k) r /= a.v; }
    return Q<DimPow<D, P>>(r);
}
template <class D> Q<DimSqrt<D>> sqrt(Q<D> a) {
    static_assert(isEvenDim<D>, "sqrt requires even dimension exponents");
    return Q<DimSqrt<D>>(std::sqrt(a.v));
}
template <class D> constexpr Q<D> abs(Q<D> a) { return Q<D>(a.v < 0 ? -a.v : a.v); }
template <class D> constexpr Q<D> min(Q<D> a, Q<D> b) { return a.v < b.v ? a : b; }
template <class D> constexpr Q<D> max(Q<D> a, Q<D> b) { return a.v > b.v ? a : b; }

// ---------------------------------------------------------------- dimension aliases (§2)
//                       T   L   M   I  Th   N   J  Ang Log
using TimeDim        = Dim< 1,  0,  0,  0,  0,  0,  0>;
using LengthDim      = Dim< 0,  1,  0,  0,  0,  0,  0>;
using MassDim        = Dim< 0,  0,  1,  0,  0,  0,  0>;
using CurrentDim     = Dim< 0,  0,  0,  1,  0,  0,  0>;
using TemperatureDim = Dim< 0,  0,  0,  0,  1,  0,  0>;
using AmountDim      = Dim< 0,  0,  0,  0,  0,  1,  0>;
using AngleDim       = Dim< 0,  0,  0,  0,  0,  0,  0, 1>;
using FrequencyDim   = Dim<-1,  0,  0,  0,  0,  0,  0>;
using AngularFreqDim = Dim<-1,  0,  0,  0,  0,  0,  0, 1>;
using EnergyDim      = Dim<-2,  2,  1,  0,  0,  0,  0>;
using PowerDim       = Dim<-3,  2,  1,  0,  0,  0,  0>;
using VoltageDim     = Dim<-3,  2,  1, -1,  0,  0,  0>;
using ChargeDim      = Dim< 1,  0,  0,  1,  0,  0,  0>;
using ResistanceDim  = Dim<-3,  2,  1, -2,  0,  0,  0>;
using CapacitanceDim = Dim< 4, -2, -1,  2,  0,  0,  0>;
using InductanceDim  = Dim<-2,  2,  1, -2,  0,  0,  0>;
using MagFluxDim     = Dim<-2,  2,  1, -1,  0,  0,  0>;
using MagFieldDim    = Dim<-2,  0,  1, -1,  0,  0,  0>;
using PressureDim    = Dim<-2, -1,  1,  0,  0,  0,  0>;
using MolarFlowDim   = Dim<-1,  0,  0,  0,  0,  1,  0>;
using VelocityDim    = Dim<-1,  1,  0,  0,  0,  0,  0>;
using AreaDim        = Dim< 0,  2,  0,  0,  0,  0,  0>;
using VolumeDim      = Dim< 0,  3,  0,  0,  0,  0,  0>;
using ForceDim       = Dim<-2,  1,  1,  0,  0,  0,  0>;
using ActionDim      = Dim<-1,  2,  1,  0,  0,  0,  0>;   // J s
using HeatCapDim     = Dim<-2,  2,  1,  0, -1,  0,  0>;   // J/K
using ThermalCondDim = Dim<-3,  1,  1,  0, -1,  0,  0>;   // W/(m K)
using PermeabilityDim= Dim<-2,  1,  1, -2,  0,  0,  0>;   // H/m
using PermittivityDim= Dim< 4, -3, -1,  2,  0,  0,  0>;   // F/m
using PerMolDim      = Dim< 0,  0,  0,  0,  0, -1,  0>;

using Dimensionless   = Q<NoDim>;
using Time            = Q<TimeDim>;
using Seconds         = Time;
using Picoseconds     = Time;   // same type; the name documents the intended scale (double ps → s)
using Nanoseconds     = Time;
using Microseconds    = Time;
using Length          = Q<LengthDim>;
using Mass            = Q<MassDim>;
using Current         = Q<CurrentDim>;
using Temperature     = Q<TemperatureDim>;
using Amount          = Q<AmountDim>;
using Angle           = Q<AngleDim>;
using Frequency       = Q<FrequencyDim>;
using AngularFrequency= Q<AngularFreqDim>;
using Energy          = Q<EnergyDim>;
using Power           = Q<PowerDim>;
using Voltage         = Q<VoltageDim>;
using Charge          = Q<ChargeDim>;
using Resistance      = Q<ResistanceDim>;
using Capacitance     = Q<CapacitanceDim>;
using Inductance      = Q<InductanceDim>;
using MagneticFlux    = Q<MagFluxDim>;
using MagneticField   = Q<MagFieldDim>;
using Pressure        = Q<PressureDim>;
using MolarFlow       = Q<MolarFlowDim>;
using Velocity        = Q<VelocityDim>;
using Area            = Q<AreaDim>;
using Volume          = Q<VolumeDim>;
using Force           = Q<ForceDim>;
using Action          = Q<ActionDim>;
using HeatCapacity    = Q<HeatCapDim>;
using ThermalConductivity = Q<ThermalCondDim>;

// Angular frequency conversions — the only way to cross the rad/s ↔ Hz boundary (§1).
constexpr AngularFrequency omega(Frequency f) { return AngularFrequency(2.0 * std::numbers::pi * f.v); }
constexpr Frequency freq(AngularFrequency w) { return Frequency(w.v / (2.0 * std::numbers::pi)); }
// Angle helpers: rad pseudo-dimension is stripped explicitly.
constexpr double radians(Angle a) { return a.v; }
constexpr Angle fromRadians(double r) { return Angle(r); }
constexpr Angle fromDegrees(double d) { return Angle(d * std::numbers::pi / 180.0); }
constexpr double degrees(Angle a) { return a.v * 180.0 / std::numbers::pi; }

// ---------------------------------------------------------------- literals (§2)
namespace literals {
#define QXL_UDL(name, Type, scale)                                                                 \
    constexpr Type operator""_##name(long double x) { return Type(static_cast<double>(x) * (scale)); } \
    constexpr Type operator""_##name(unsigned long long x) { return Type(static_cast<double>(x) * (scale)); }
QXL_UDL(ps, Time, 1e-12) QXL_UDL(ns, Time, 1e-9) QXL_UDL(us, Time, 1e-6) QXL_UDL(ms, Time, 1e-3) QXL_UDL(s, Time, 1.0)
QXL_UDL(Hz, Frequency, 1.0) QXL_UDL(kHz, Frequency, 1e3) QXL_UDL(MHz, Frequency, 1e6) QXL_UDL(GHz, Frequency, 1e9)
QXL_UDL(rad_s, AngularFrequency, 1.0)
QXL_UDL(J, Energy, 1.0) QXL_UDL(eV, Energy, 1.602176634e-19) QXL_UDL(meV, Energy, 1.602176634e-22) QXL_UDL(ueV, Energy, 1.602176634e-25)
QXL_UDL(uK, Temperature, 1e-6) QXL_UDL(mK, Temperature, 1e-3) QXL_UDL(K, Temperature, 1.0)
QXL_UDL(W, Power, 1.0) QXL_UDL(mW, Power, 1e-3) QXL_UDL(uW, Power, 1e-6) QXL_UDL(nW, Power, 1e-9)
QXL_UDL(pW, Power, 1e-12) QXL_UDL(fW, Power, 1e-15) QXL_UDL(aW, Power, 1e-18)
QXL_UDL(V, Voltage, 1.0) QXL_UDL(mV, Voltage, 1e-3) QXL_UDL(uV, Voltage, 1e-6)
QXL_UDL(A, Current, 1.0) QXL_UDL(mA, Current, 1e-3) QXL_UDL(uA, Current, 1e-6) QXL_UDL(nA, Current, 1e-9)
QXL_UDL(Wb, MagneticFlux, 1.0)
QXL_UDL(F, Capacitance, 1.0) QXL_UDL(pF, Capacitance, 1e-12) QXL_UDL(fF, Capacitance, 1e-15) QXL_UDL(aF, Capacitance, 1e-18)
QXL_UDL(H, Inductance, 1.0) QXL_UDL(nH, Inductance, 1e-9) QXL_UDL(pH, Inductance, 1e-12)
QXL_UDL(ohm, Resistance, 1.0) QXL_UDL(kohm, Resistance, 1e3)
QXL_UDL(Pa, Pressure, 1.0) QXL_UDL(mbar, Pressure, 100.0) QXL_UDL(bar, Pressure, 1e5)
QXL_UDL(mol_s, MolarFlow, 1.0) QXL_UDL(mmol_s, MolarFlow, 1e-3) QXL_UDL(umol_s, MolarFlow, 1e-6)
QXL_UDL(m, Length, 1.0) QXL_UDL(mm, Length, 1e-3) QXL_UDL(um, Length, 1e-6) QXL_UDL(nm, Length, 1e-9)
QXL_UDL(kg, Mass, 1.0) QXL_UDL(u, Mass, 1.66053906660e-27)
QXL_UDL(rad, Angle, 1.0) QXL_UDL(deg, Angle, std::numbers::pi / 180.0)
#undef QXL_UDL
} // namespace literals

} // namespace qlab::units
