// Spec 19 §5.1/§5.2 — numeric presentation (see Format.hpp).
#include "UI/Format.hpp"
#include "UI/Theme.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>

namespace qlab::ui::format {
namespace {

const units::UnitCatalog& cat() {
    return units::UnitCatalog::global();
}

// "dB", "dBm", "dBc", "dBc/Hz" are log ratios: the stored number IS the displayed number.
bool isLogSymbol(std::string_view u) {
    return u.size() >= 2 && (u[0] == 'd' && u[1] == 'B');
}

} // namespace

std::string number(double v, int digits) {
    if (!std::isfinite(v))
        return "—";
    return units::UnitCatalog::formatNumber(v, digits);
}

units::FormatContext contextFor(std::string_view unit) {
    if (unit == "s")
        return units::FormatContext::GateDuration;
    if (unit == "K")
        return units::FormatContext::Cryogenic;
    if (unit == "dBm")
        return units::FormatContext::RfPower;
    return units::FormatContext::Default;
}

std::string value(double si, std::string_view unit, int digits, units::FormatContext ctx) {
    if (!std::isfinite(si))
        return "—";
    if (unit.empty())
        return number(si, digits);
    if (isLogSymbol(unit))
        return number(si, digits) + " " + std::string(unit);
    const units::UnitDef* def = cat().find(unit);
    if (def == nullptr)
        return number(si, digits) + " " + std::string(unit);
    return cat().format(si, def->dim, digits, ctx);
}

std::string prefixedUnit(double si, std::string_view unit) {
    const std::string s = value(si, unit);
    const std::size_t sp = s.rfind(' ');
    return sp == std::string::npos ? std::string{} : s.substr(sp + 1);
}

std::string withSigma(double si, double sigma, std::string_view unit, int digits) {
    if (!std::isfinite(si))
        return "—";
    const std::string full = value(si, unit, digits);
    if (!std::isfinite(sigma) || sigma <= 0.0)
        return full;
    // Both numbers in the SAME prefixed unit so the ± is readable (spec 19 §5.1).
    const std::size_t sp = full.rfind(' ');
    const std::string symbol = sp == std::string::npos ? std::string{} : full.substr(sp + 1);
    const std::string head = sp == std::string::npos ? full : full.substr(0, sp);
    if (symbol.empty())
        return head + " ± " + number(sigma, 2);
    const auto scaled = cat().toUnit(si, symbol);
    const auto scaledSigma = cat().toUnit(sigma, symbol);
    if (!scaled || !scaledSigma)
        return head + " ± " + number(sigma, 2) + " " + symbol;
    // toUnit subtracts the unit offset; a σ is a difference, so re-add it.
    const units::UnitDef* def = cat().find(symbol);
    const double sig = *scaledSigma + (def != nullptr ? def->offset / def->scale : 0.0);
    return head + " ± " + number(sig, 2) + " " + symbol;
}

std::string integer(std::uint64_t v) {
    std::string digits = std::to_string(v);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    const std::size_t lead = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i == lead || (i > lead && (i - lead) % 3 == 0))
            out += " "; // narrow no-break space
        out += digits[i];
    }
    return out;
}

std::string duration(double seconds) {
    if (!std::isfinite(seconds))
        return "—";
    const double a = std::fabs(seconds);
    if (a < 60.0)
        return value(seconds, "s");
    const auto total = static_cast<std::int64_t>(std::llround(a));
    const std::int64_t h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    const char* sign = seconds < 0.0 ? "-" : "";
    if (h > 0)
        return std::format("{}{} h {:02} min", sign, h, m);
    return std::format("{}{} min {:02} s", sign, m, s);
}

std::string bytes(double count) {
    if (!std::isfinite(count))
        return "—";
    static constexpr std::array<const char*, 6> kUnits{"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double v = count;
    std::size_t i = 0;
    while (std::fabs(v) >= 1024.0 && i + 1 < kUnits.size()) {
        v /= 1024.0;
        ++i;
    }
    return number(v, i == 0 ? 6 : 4) + " " + kUnits[i];
}

std::string percent(double fraction, int decimals) {
    if (!std::isfinite(fraction))
        return "—";
    return std::format("{:.{}f} %", 100.0 * fraction, std::max(0, decimals));
}

Result<double> parse(std::string_view text, std::string_view defaultUnit) {
    if (defaultUnit.empty()) {
        // A dimensionless field takes a bare number; the catalog refuses one without a default.
        std::string trimmed(text);
        const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
        trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), notSpace));
        trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), notSpace).base(),
                      trimmed.end());
        double v = 0.0;
        const auto [ptr, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), v);
        if (ec == std::errc{} && ptr == trimmed.data() + trimmed.size())
            return v;
    }
    const auto parsed = cat().parse(text, defaultUnit);
    if (!parsed)
        return fail(
            err::BadToken,
            std::format("'{}' is not a number{}", text,
                        defaultUnit.empty() ? "" : std::format(" with a unit of {}", defaultUnit)));
    if (!defaultUnit.empty()) {
        const units::UnitDef* want = cat().find(defaultUnit);
        if (want != nullptr && parsed->dim != want->dim)
            return fail(err::BadToken,
                        std::format("unit '{}' has the wrong dimension; this field is in {}",
                                    parsed->symbol, defaultUnit));
    }
    return parsed->si;
}

double dragScale(DragModifiers mods) {
    // Spec 19 §5.2: Shift = fine (×0.1), Alt = coarser (×10); both cancel out to the plain rate.
    return (mods.shift ? 0.1 : 1.0) * (mods.alt ? 10.0 : 1.0);
}

double applyDrag(double current, double pixels, double step, DragModifiers mods, double lo,
                 double hi) {
    const double next = current + pixels * step * dragScale(mods);
    if (!std::isfinite(next))
        return current;
    return std::clamp(next, std::min(lo, hi), std::max(lo, hi));
}

} // namespace qlab::ui::format
