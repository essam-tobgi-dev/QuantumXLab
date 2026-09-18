// Spec 21 §4 — readout text (see Format.hpp). Unit prefixes come from the units catalog (spec 05 §4).
#include "Viz/Math/Format.hpp"
#include "Units/Units.hpp"
#include "Viz/Math/Phase.hpp"
#include <cmath>
#include <cstdio>

namespace qlab::viz::math {

std::string formatSig(double value, int sig) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
    if (std::abs(value) < 1e-300) return "0";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*g", sig < 1 ? 1 : sig, value);
    return buf;
}

std::string formatWithUnit(double value, std::string_view unit, int sig) {
    std::string s = formatSig(value, sig);
    if (!unit.empty()) {
        s += ' ';
        s += unit;
    }
    return s;
}

std::string formatTime(double seconds, int sig) { return units::fmt(units::Time{seconds}, sig); }
std::string formatFrequency(double hertz, int sig) { return units::fmt(units::Frequency{hertz}, sig); }

std::string formatProbability(double p, bool percent) {
    return percent ? formatSig(100.0 * p) + " %" : formatSig(p);
}

std::string formatCartesian(num::Complex a, int sig) {
    const double re = a.real(), im = a.imag();
    std::string s = formatSig(re, sig);
    s += im < 0.0 ? " - " : " + ";
    s += formatSig(std::abs(im), sig);
    s += 'i';
    return s;
}

std::string formatPolar(num::Complex a, int sig) {
    const double r = std::abs(a);
    if (r < 1e-300) return "0";
    return formatSig(r, sig) + " e^{i(" + formatAngle(phaseOf(a)) + ")}";
}

std::string bitString(std::uint64_t index, std::size_t nbits) {
    std::string s(nbits, '0');
    for (std::size_t k = 0; k < nbits && k < 64; ++k)
        if ((index >> k) & 1u) s[nbits - 1 - k] = '1';   // qubit k is character n−1−k
    return s;
}

namespace {
std::string body(std::uint64_t index, std::size_t nbits, bool decimal) {
    return decimal ? std::to_string(index) : bitString(index, nbits);
}
} // namespace

std::string ketLabel(std::uint64_t index, std::size_t nbits, bool decimal, bool ascii) {
    return "|" + body(index, nbits, decimal) + (ascii ? ">" : "\xE2\x9F\xA9"); // U+27E9
}

std::string braLabel(std::uint64_t index, std::size_t nbits, bool decimal, bool ascii) {
    return (ascii ? std::string("<") : std::string("\xE2\x9F\xA8")) + body(index, nbits, decimal) + "|"; // U+27E8
}

} // namespace qlab::viz::math
