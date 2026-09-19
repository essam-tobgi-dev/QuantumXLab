#include "Units/UnitCatalog.hpp"
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>

namespace qlab::units {

namespace {
DimSig sig(int t, int l, int m, int i, int th, int n, int j, int ang = 0, int lg = 0) {
    return DimSig{{t, l, m, i, th, n, j, ang, lg}};
}
std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}
} // namespace

const UnitCatalog& UnitCatalog::global() {
    static const UnitCatalog c;
    return c;
}

UnitCatalog::UnitCatalog() {
    const DimSig T = sig(1, 0, 0, 0, 0, 0, 0), F = sig(-1, 0, 0, 0, 0, 0, 0),
                 W = sig(-1, 0, 0, 0, 0, 0, 0, 1);
    const DimSig E = sig(-2, 2, 1, 0, 0, 0, 0), P = sig(-3, 2, 1, 0, 0, 0, 0),
                 Th = sig(0, 0, 0, 0, 1, 0, 0);
    const DimSig V = sig(-3, 2, 1, -1, 0, 0, 0), I = sig(0, 0, 0, 1, 0, 0, 0),
                 R = sig(-3, 2, 1, -2, 0, 0, 0);
    const DimSig C = sig(4, -2, -1, 2, 0, 0, 0), L = sig(-2, 2, 1, -2, 0, 0, 0),
                 Wb = sig(-2, 2, 1, -1, 0, 0, 0);
    const DimSig Pa = sig(-2, -1, 1, 0, 0, 0, 0), MF = sig(-1, 0, 0, 0, 0, 1, 0),
                 Len = sig(0, 1, 0, 0, 0, 0, 0);
    const DimSig M = sig(0, 0, 1, 0, 0, 0, 0), Ang = sig(0, 0, 0, 0, 0, 0, 0, 1),
                 None = sig(0, 0, 0, 0, 0, 0, 0);
    const DimSig Bfield = sig(-2, 0, 1, -1, 0, 0, 0);
    // Base (prefixable) units
    add({"s", T, 1.0, 0.0, true});
    add({"Hz", F, 1.0, 0.0, true});
    add({"rad/s", W, 1.0, 0.0, false});
    add({"J", E, 1.0, 0.0, true});
    add({"eV", E, 1.602176634e-19, 0.0, true});
    add({"W", P, 1.0, 0.0, true});
    add({"K", Th, 1.0, 0.0, true});
    add({"°C", Th, 1.0, 273.15, false});
    add({"V", V, 1.0, 0.0, true});
    add({"A", I, 1.0, 0.0, true});
    add({"Ω", R, 1.0, 0.0, true});
    add({"ohm", R, 1.0, 0.0, true});
    add({"F", C, 1.0, 0.0, true});
    add({"H", L, 1.0, 0.0, true});
    add({"Wb", Wb, 1.0, 0.0, true});
    add({"Φ0", Wb, 2.067833848e-15, 0.0, false});
    add({"Phi0", Wb, 2.067833848e-15, 0.0, false});
    add({"Pa", Pa, 1.0, 0.0, true});
    add({"bar", Pa, 1e5, 0.0, true});
    add({"mbar", Pa, 100.0, 0.0, false});
    add({"mol/s", MF, 1.0, 0.0, true});
    add({"m", Len, 1.0, 0.0, true});
    add({"g", M, 1e-3, 0.0, true});
    add({"kg", M, 1.0, 0.0, false});
    add({"u", M, 1.66053906660e-27, 0.0, false});
    add({"T", Bfield, 1.0, 0.0, true});
    add({"rad", Ang, 1.0, 0.0, true});
    add({"deg", Ang, std::numbers::pi / 180.0, 0.0, false});
    add({"°", Ang, std::numbers::pi / 180.0, 0.0, false});
    add({"", None, 1.0, 0.0, false});
    add({"%", None, 0.01, 0.0, false});
    add({"ppm", None, 1e-6, 0.0, false});
    add({"min", T, 60.0, 0.0, false});
    add({"h", T, 3600.0, 0.0, false});
    // Log units
    UnitDef dbm{"dBm", sig(0, 0, 0, 0, 0, 0, 0, 0, 1), 1.0, 0.0, false, true};
    add(dbm);
    UnitDef db{"dB", sig(0, 0, 0, 0, 0, 0, 0, 0, 2), 1.0, 0.0, false, true};
    add(db);
    // Explicit prefixed forms so that find("GHz") etc. resolve directly and parse fast.
    std::vector<UnitDef> base = units_;
    for (const auto& u : base) {
        if (!u.prefixAllowed)
            continue;
        for (const auto& [p, s] : kPrefixes) {
            if (*p == '\0')
                continue;
            UnitDef d = u;
            d.symbol = std::string(p) + u.symbol;
            d.scale = u.scale * s;
            d.prefixAllowed = false;
            add(d);
            if (std::string_view(p) == "µ") {
                UnitDef d2 = d;
                d2.symbol = std::string("u") + u.symbol;
                add(d2);
            }
        }
    }
}

void UnitCatalog::add(UnitDef def) {
    for (auto& u : units_)
        if (u.symbol == def.symbol) {
            u = def;
            return;
        }
    units_.push_back(std::move(def));
}
const UnitDef* UnitCatalog::find(std::string_view symbol) const {
    for (const auto& u : units_)
        if (u.symbol == symbol)
            return &u;
    return nullptr;
}
std::vector<const UnitDef*> UnitCatalog::forDimension(DimSig d) const {
    std::vector<const UnitDef*> r;
    for (const auto& u : units_)
        if (u.dim == d)
            r.push_back(&u);
    return r;
}

std::string UnitCatalog::formatNumber(double v, int digits) {
    if (digits < 1)
        digits = 1;
    if (v == 0.0)
        return "0";
    if (!std::isfinite(v))
        return std::isnan(v) ? "nan" : (v > 0 ? "inf" : "-inf");
    // Round to `digits` significant figures, then print without trailing zeros.
    double mag = std::floor(std::log10(std::fabs(v)));
    double scale = std::pow(10.0, digits - 1 - mag);
    double r = std::round(v * scale) / scale;
    int decimals = static_cast<int>(std::max(0.0, digits - 1 - mag));
    if (std::fabs(r) >= 1e15 || std::fabs(r) < 1e-4)
        return std::format("{:.{}g}", r, digits);
    std::string s = std::format("{:.{}f}", r, decimals);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0')
            s.pop_back();
        if (!s.empty() && s.back() == '.')
            s.pop_back();
    }
    if (s == "-0")
        s = "0";
    return s;
}

std::string UnitCatalog::format(double si, DimSig d, int digits, FormatContext ctx) const {
    const DimSig T = sig(1, 0, 0, 0, 0, 0, 0), Th = sig(0, 0, 0, 0, 1, 0, 0),
                 P = sig(-3, 2, 1, 0, 0, 0, 0);
    const DimSig E = sig(-2, 2, 1, 0, 0, 0, 0), None = sig(0, 0, 0, 0, 0, 0, 0);
    const DimSig Ang = sig(0, 0, 0, 0, 0, 0, 0, 1);
    if (d == None)
        return formatNumber(si, digits);
    // Angles read in degrees (a cutaway at 30°, a trap-axis tilt of 5°); radians stay the SI
    // value underneath and remain accepted on input.
    if (d == Ang)
        return formatNumber(si * 180.0 / std::numbers::pi, digits) + " °";
    if (ctx == FormatContext::RfPower && d == P) {
        if (si <= 0.0)
            return "-inf dBm";
        return formatNumber(10.0 * std::log10(si / 1e-3), digits) + " dBm";
    }
    if (ctx == FormatContext::Energy_GHz && d == E) {
        double ghz = si / 6.62607015e-34 / 1e9;
        return formatNumber(ghz, digits) + " GHz";
    }
    if (ctx == FormatContext::GateDuration && d == T) {
        double a = std::fabs(si);
        if (a < 1e-6)
            return formatNumber(si * 1e9, digits) + " ns";
        if (a < 1e-3)
            return formatNumber(si * 1e6, digits) + " µs";
    }
    if (ctx == FormatContext::Cryogenic && d == Th && std::fabs(si) < 1.0)
        return formatNumber(si * 1e3, digits) + " mK";
    // Choose the base prefixable unit for this dimension (first registered with prefixAllowed).
    const UnitDef* base = nullptr;
    for (const auto& u : units_)
        if (u.dim == d && u.prefixAllowed) {
            base = &u;
            break;
        }
    if (!base) {
        auto lst = forDimension(d);
        if (lst.empty())
            return formatNumber(si, digits);
        return formatNumber((si - lst[0]->offset) / lst[0]->scale, digits) + " " + lst[0]->symbol;
    }
    double x = si / base->scale;
    if (x == 0.0 || !std::isfinite(x))
        return formatNumber(x, digits) + " " + base->symbol;
    // Temperature never uses prefixes above K; time never above s; frequency allows k M G T.
    double ax = std::fabs(x);
    const char* pre = "";
    double ps = 1.0;
    for (const auto& [p, s] : kPrefixes) {
        if (ax / s >= 1.0 && ax / s < 1000.0) {
            pre = p;
            ps = s;
            break;
        }
    }
    bool noLarge = (d == T || d == Th);
    if (noLarge && ps > 1.0) {
        pre = "";
        ps = 1.0;
    }
    if (ax >= 1000.0 && ps == 1.0 && !noLarge) {
        pre = "P";
        ps = 1e15;
        if (ax < 1e15) {
            pre = "T";
            ps = 1e12;
        }
    }
    if (ax < 1e-18) {
        pre = "a";
        ps = 1e-18;
    }
    return formatNumber(x / ps, digits) + " " + pre + base->symbol;
}
std::string UnitCatalog::format(PowerDbm p, int digits) const {
    return formatNumber(p.v, digits) + " dBm";
}
std::string UnitCatalog::format(GainDb g, int digits) const {
    return formatNumber(g.v, digits) + " dB";
}

Result<std::string> UnitCatalog::formatIn(double si, std::string_view symbol, int digits) const {
    auto v = toUnit(si, symbol);
    if (!v)
        return std::unexpected(v.error());
    return formatNumber(*v, digits) + (symbol.empty() ? "" : " " + std::string(symbol));
}
Result<double> UnitCatalog::toUnit(double si, std::string_view symbol) const {
    const UnitDef* u = find(symbol);
    if (!u)
        return fail(ErrorCode::Parse, "unknown unit '" + std::string(symbol) + "'");
    if (u->isLog)
        return si;
    return (si - u->offset) / u->scale;
}
Result<double> UnitCatalog::fromUnit(double value, std::string_view symbol) const {
    const UnitDef* u = find(symbol);
    if (!u)
        return fail(ErrorCode::Parse, "unknown unit '" + std::string(symbol) + "'");
    if (u->isLog)
        return value;
    return value * u->scale + u->offset;
}

Result<UnitCatalog::Parsed> UnitCatalog::parse(std::string_view text,
                                               std::string_view defaultUnit) const {
    std::string_view s = trim(text);
    if (s.empty())
        return fail(ErrorCode::Parse, "empty quantity");
    // Number prefix: accept unicode minus.
    std::string num;
    std::size_t i = 0;
    if (s.starts_with("−")) {
        num += '-';
        i += std::string_view("−").size();
    }
    while (i < s.size()) {
        char c = s[i];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+' ||
            c == 'e' || c == 'E') {
            if ((c == 'e' || c == 'E') &&
                (i + 1 >= s.size() || !(std::isdigit(static_cast<unsigned char>(s[i + 1])) ||
                                        s[i + 1] == '-' || s[i + 1] == '+')))
                break;
            num += c;
            ++i;
        } else
            break;
    }
    double value = 0.0;
    auto [ptr, ec] = std::from_chars(num.data(), num.data() + num.size(), value);
    if (ec != std::errc{} || ptr != num.data() + num.size())
        return fail(ErrorCode::Parse, "cannot parse number in '" + std::string(text) + "'");
    std::string_view sym = trim(s.substr(i));
    if (sym.empty()) {
        if (defaultUnit.empty())
            return fail(ErrorCode::Parse, "bare number '" + std::string(text) +
                                              "' has no unit and no default unit was given");
        sym = defaultUnit;
    }
    const UnitDef* u = find(sym);
    if (!u)
        return fail(ErrorCode::Parse,
                    "unknown unit '" + std::string(sym) + "' in '" + std::string(text) + "'");
    return Parsed{u->isLog ? value : value * u->scale + u->offset, u->symbol, u->dim};
}

core::Json UnitCatalog::toJson(double si, std::string_view symbol, int /*digits*/) const {
    double v = si;
    if (const UnitDef* u = find(symbol); u && !u->isLog)
        v = (si - u->offset) / u->scale;
    return core::Json{{"value", v}, {"unit", std::string(symbol)}};
}
Result<double> UnitCatalog::fromJson(const core::Json& j, DimSig expectedDim) const {
    if (j.is_number())
        return j.get<double>();
    if (!j.is_object() || !j.contains("value"))
        return fail(ErrorCode::Parse, "quantity must be a number or {value, unit}");
    std::string sym = j.value("unit", "");
    const UnitDef* u = find(sym);
    if (!u)
        return fail(ErrorCode::Parse, "unknown unit '" + sym + "'");
    if (u->dim != expectedDim)
        return fail(ErrorCode::InvalidArgument, "unit '" + sym + "' has the wrong dimension");
    double v = j["value"].get<double>();
    return u->isLog ? v : v * u->scale + u->offset;
}

} // namespace qlab::units
