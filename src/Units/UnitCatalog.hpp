#pragma once
// Spec 05 §6 — runtime unit catalog for display, parsing, and JSON quantities.
#include "Core/Error.hpp"
#include "Core/Json.hpp"
#include "Units/Dimension.hpp"
#include "Units/LogUnits.hpp"
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::units {

// Runtime dimension signature (mirrors Dim<> exponents).
struct DimSig {
    std::array<int, 9> e{}; // t l m i th n j ang log
    constexpr bool operator==(const DimSig&) const = default;
    template <class D> static constexpr DimSig of() {
        return DimSig{{D::t, D::l, D::m, D::i, D::th, D::n, D::j, D::ang, D::log}};
    }
    template <class D> static constexpr DimSig of(Q<D>) { return of<D>(); }
};

struct UnitDef {
    std::string symbol; // "GHz", "mK", "dBm", "ns"
    DimSig dim;
    double scale = 1.0;         // SI = scale * value + offset
    double offset = 0.0;        // °C etc.
    bool prefixAllowed = false; // base symbol that accepts SI prefixes (e.g. "Hz", "s", "W")
    bool isLog = false;         // dBm / dB
};

enum class FormatContext { Default, GateDuration, Cryogenic, RfPower, Energy_GHz };

class UnitCatalog {
  public:
    static const UnitCatalog& global();
    UnitCatalog();

    void add(UnitDef def);
    const UnitDef* find(std::string_view symbol) const;
    std::vector<const UnitDef*> forDimension(DimSig d) const;

    // Format an SI value of dimension `d` with auto prefix. digits = significant figures.
    std::string format(double si, DimSig d, int digits = 4,
                       FormatContext ctx = FormatContext::Default) const;
    template <class D>
    std::string format(Q<D> q, int digits = 4, FormatContext ctx = FormatContext::Default) const {
        return format(q.v, DimSig::of<D>(), digits, ctx);
    }
    std::string format(PowerDbm p, int digits = 4) const;
    std::string format(GainDb g, int digits = 4) const;
    // Format in a specific unit symbol: format(3.2e-7, "ns") -> "320 ns".
    Result<std::string> formatIn(double si, std::string_view symbol, int digits = 4) const;

    // Convert an SI value to the given unit symbol / from it.
    Result<double> toUnit(double si, std::string_view symbol) const;
    Result<double> fromUnit(double value, std::string_view symbol) const;

    // Parse "4.5 GHz", "-20 dB", "320ns". Bare numbers take `defaultUnit` (may be empty → Parse
    // error).
    struct Parsed {
        double si;
        std::string symbol;
        DimSig dim;
    };
    Result<Parsed> parse(std::string_view text, std::string_view defaultUnit = {}) const;
    template <class D>
    Result<Q<D>> parseAs(std::string_view text, std::string_view defaultUnit = {}) const {
        auto p = parse(text, defaultUnit);
        if (!p)
            return std::unexpected(p.error());
        if (p->dim != DimSig::of<D>())
            return fail(ErrorCode::InvalidArgument,
                        "unit '" + p->symbol + "' has the wrong dimension");
        return Q<D>(p->si);
    }

    // JSON quantity { "value": 4.812, "unit": "GHz" }
    core::Json toJson(double si, std::string_view symbol, int digits = 17) const;
    template <class D> core::Json toJson(Q<D> q, std::string_view symbol) const {
        return toJson(q.v, symbol);
    }
    Result<double> fromJson(const core::Json& j, DimSig expectedDim) const;
    template <class D> Result<Q<D>> fromJson(const core::Json& j) const {
        auto r = fromJson(j, DimSig::of<D>());
        if (!r)
            return std::unexpected(r.error());
        return Q<D>(*r);
    }

    static std::string formatNumber(double v, int digits);

  private:
    std::vector<UnitDef> units_;
    static constexpr std::array<std::pair<const char*, double>, 12> kPrefixes{{{"a", 1e-18},
                                                                               {"f", 1e-15},
                                                                               {"p", 1e-12},
                                                                               {"n", 1e-9},
                                                                               {"µ", 1e-6},
                                                                               {"m", 1e-3},
                                                                               {"", 1.0},
                                                                               {"k", 1e3},
                                                                               {"M", 1e6},
                                                                               {"G", 1e9},
                                                                               {"T", 1e12},
                                                                               {"P", 1e15}}};
};

// Convenience wrappers on the global catalog.
template <class D>
std::string fmt(Q<D> q, int digits = 4, FormatContext ctx = FormatContext::Default) {
    return UnitCatalog::global().format(q, digits, ctx);
}
inline std::string fmt(PowerDbm p, int digits = 4) {
    return UnitCatalog::global().format(p, digits);
}
inline std::string fmt(GainDb g, int digits = 4) {
    return UnitCatalog::global().format(g, digits);
}

} // namespace qlab::units
