#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "Units/Units.hpp"
#include <cmath>
#include <type_traits>

using namespace qlab;
using namespace qlab::units;
using namespace qlab::units::literals;
using Catch::Approx;

// ---- static dimension algebra (spec 05 §8)
static_assert(std::is_same_v<decltype(Frequency(1.0) * Time(1.0)), Dimensionless>);
static_assert(std::is_same_v<decltype(Voltage(1.0) * Current(1.0)), Power>);
static_assert(std::is_same_v<decltype(Energy(1.0) / Time(1.0)), Power>);
static_assert(std::is_same_v<decltype(Voltage(1.0) / Current(1.0)), Resistance>);
static_assert(std::is_same_v<decltype(Charge(1.0) / Voltage(1.0)), Capacitance>);
static_assert(std::is_same_v<decltype(omega(Frequency(1.0))), AngularFrequency>);
static_assert(!std::is_same_v<Frequency, AngularFrequency>);
static_assert(std::is_same_v<decltype(consts::hbar * AngularFrequency(1.0)), Q<DimMul<ActionDim, AngularFreqDim>>>);
static_assert(std::is_same_v<decltype(sqrt(Area(4.0))), Length>);
static_assert(std::is_same_v<decltype(pow<2>(Length(2.0))), Area>);
static_assert(std::is_same_v<decltype(1.0 / Time(2.0)), Frequency>);
// Negative compile test: Frequency + AngularFrequency must not compile.
template <class A, class B, class = void> struct Addable : std::false_type {};
template <class A, class B> struct Addable<A, B, std::void_t<decltype(std::declval<A>() + std::declval<B>())>> : std::true_type {};
static_assert(!Addable<Frequency, AngularFrequency>::value);
static_assert(!Addable<Time, Frequency>::value);
static_assert(Addable<Frequency, Frequency>::value);
static_assert(Addable<PowerDbm, GainDb>::value);
static_assert(!Addable<PowerDbm, PowerDbm>::value);
static_assert(std::is_convertible_v<Dimensionless, double>);
static_assert(!std::is_convertible_v<Frequency, double>);
static_assert(1.0_GHz == Frequency(1e9));
static_assert(320.0_ns == Time(3.2e-7));
static_assert(consts::Phi0.v > 2.0678e-15 && consts::Phi0.v < 2.0679e-15);

TEST_CASE("angular frequency conversions carry 2π explicitly") {
    Frequency f = 4.8_GHz;
    AngularFrequency w = omega(f);
    REQUIRE(w.v == Approx(2 * M_PI * 4.8e9));
    REQUIRE(freq(w).v == Approx(f.v));
    REQUIRE((sqrt(Energy(4.0) * Energy(9.0))).v == Approx(6.0));
    Dimensionless d = 2.0_GHz * 1.0_ns;
    double x = d;
    REQUIRE(x == Approx(2.0));
}

TEST_CASE("constants: Phi0 = h/2e and R_K = h/e^2 to 1e-15") {
    REQUIRE(consts::Phi0.v == Approx(2.067833848e-15).epsilon(1e-9));
    REQUIRE(consts::R_K.v == Approx(25812.80745).epsilon(1e-9));
    REQUIRE(consts::hbar.v == Approx(1.054571817e-34).epsilon(1e-9));
    REQUIRE((consts::h.v / (2 * consts::e.v)) == Approx(consts::Phi0.v).epsilon(1e-15));
    REQUIRE((consts::h.v / (consts::e.v * consts::e.v)) == Approx(consts::R_K.v).epsilon(1e-15));
    REQUIRE(consts::kB_over_h.v == Approx(20.8366e9).epsilon(1e-4));
    REQUIRE(thermalFrequency(20.0_mK).v == Approx(0.4167e9).epsilon(1e-3));
    REQUIRE(fluxQuanta(fromFluxQuanta(0.25)) == Approx(0.25));
}

TEST_CASE("dBm <-> W round trips and chains") {
    REQUIRE(fromDbm(0.0_dBm).v == Approx(1e-3));
    REQUIRE(toDbm(1.0_mW).v == Approx(0.0).margin(1e-12));
    for (double d : {-130.0, -60.0, -20.0, 0.0, 13.0, 30.0})
        REQUIRE(toDbm(fromDbm(PowerDbm(d))).v == Approx(d).margin(1e-9));
    PowerDbm p = 0.0_dBm; GainDb att = -20.0_dB;
    PowerDbm out = p + att + att;
    REQUIRE(fromDbm(out).v == Approx(1e-3 * 1e-4));
    REQUIRE((att + att).linear() == Approx(1e-4));
    REQUIRE((PowerDbm(10.0) - PowerDbm(3.0)).v == Approx(7.0));
    REQUIRE(voltageDb(10.0) == Approx(20.0));
}

TEST_CASE("thermal photons <-> temperature at 5 GHz") {
    Frequency f = 5.0_GHz;
    double n20 = thermalPhotons(f, 20.0_mK);
    double x = consts::h.v * 5e9 / (consts::k_B.v * 0.020);
    REQUIRE(n20 == Approx(1.0 / (std::exp(x) - 1.0)).epsilon(1e-9));
    REQUIRE(n20 == Approx(6.1e-6).epsilon(0.05));
    REQUIRE(effectiveTemperature(f, n20).v == Approx(0.020).epsilon(1e-9));
    double n4k = thermalPhotons(f, 4.0_K);
    REQUIRE(n4k == Approx(16.2).epsilon(0.02));
    REQUIRE(effectiveTemperature(f, n4k).v == Approx(4.0).epsilon(1e-9));
    REQUIRE(thermalPhotons(f, 0.0_K) == 0.0);
    double p1 = thermalPopulation(f, 50.0_mK);
    REQUIRE(temperatureFromPopulation(f, p1).v == Approx(0.050).epsilon(1e-9));
    REQUIRE(energyFromGHz(f).v == Approx(consts::h.v * 5e9));
    REQUIRE(frequencyFromEnergy(energyFromGHz(f)).v == Approx(5e9));
}

TEST_CASE("bounded types reject out-of-range and clamp within eps") {
    REQUIRE(Probability::make(0.5)->v == 0.5);
    REQUIRE(Probability::make(1.0 + 1e-13)->v == 1.0);
    REQUIRE(Probability::make(-1e-13)->v == 0.0);
    REQUIRE_FALSE(Probability::make(1.001).has_value());
    REQUIRE(Probability::make(1.001).error().code == ErrorCode::OutOfRange);
    REQUIRE_FALSE(Fidelity::make(-0.1).has_value());
    REQUIRE_FALSE(Population::make(2.0).has_value());
    REQUIRE(Infidelity(2.3e-3).fidelity().v == Approx(1 - 2.3e-3));
    REQUIRE(infidelityOf(Fidelity::unsafe(0.99)).v == Approx(0.01));
    static_assert(!std::is_convertible_v<Probability, Fidelity>);
}

TEST_CASE("catalog auto-prefix and contexts") {
    const auto& c = UnitCatalog::global();
    REQUIRE(c.format(4.812e9, DimSig::of<FrequencyDim>()) == "4.812 GHz");
    REQUIRE(c.format(320.0_ns, 4, FormatContext::GateDuration) == "320 ns");
    REQUIRE(c.format(1.5_us, 4, FormatContext::GateDuration) == "1.5 µs");
    REQUIRE(c.format(0.015_K, 4, FormatContext::Cryogenic) == "15 mK");
    REQUIRE(c.format(0.015_K) == "15 mK");
    REQUIRE(c.format(1e-15_W, 4, FormatContext::RfPower) == "-120 dBm");
    REQUIRE(c.format(1e-15_W) == "1 fW");
    REQUIRE(c.format(3.5_K) == "3.5 K");
    REQUIRE(c.format(293.0_K) == "293 K");
    REQUIRE(c.format(64.4_fF) == "64.4 fF");
    REQUIRE(c.format(PowerDbm(-20.0)) == "-20 dBm");
    REQUIRE(c.format(GainDb(-20.0)) == "-20 dB");
    REQUIRE(c.format(Dimensionless(0.98)) == "0.98");
    REQUIRE(c.formatIn(3.2e-7, "ns").value() == "320 ns");
    REQUIRE(c.format(energyFromGHz(15.0_GHz), 4, FormatContext::Energy_GHz) == "15 GHz");
    REQUIRE(c.format(2.5e12, DimSig::of<FrequencyDim>()) == "2.5 THz");
}

TEST_CASE("catalog parse") {
    const auto& c = UnitCatalog::global();
    REQUIRE(c.parseAs<FrequencyDim>("4.5 GHz")->v == Approx(4.5e9));
    REQUIRE(c.parseAs<TimeDim>("320ns")->v == Approx(3.2e-7));
    REQUIRE(c.parse("-20 dB")->si == Approx(-20.0));
    REQUIRE(c.parse("−20 dBm")->si == Approx(-20.0));
    REQUIRE(c.parse("15 mK")->si == Approx(0.015));
    REQUIRE(c.parse("25 °C")->si == Approx(298.15));
    REQUIRE(c.parse("1.2 µs")->si == Approx(1.2e-6));
    REQUIRE(c.parse("1.2 us")->si == Approx(1.2e-6));
    REQUIRE(c.parse("42", "ns")->si == Approx(42e-9));
    REQUIRE(c.parse("1 mmol/s")->si == Approx(1e-3));
    REQUIRE_FALSE(c.parse("42").has_value());
    REQUIRE_FALSE(c.parse("4 furlongs").has_value());
    REQUIRE_FALSE(c.parseAs<TimeDim>("4 GHz").has_value());
}

TEST_CASE("catalog JSON round trip for every registered unit") {
    const auto& c = UnitCatalog::global();
    for (const char* sym : {"GHz", "MHz", "kHz", "Hz", "ns", "us", "µs", "ms", "s", "mK", "K", "eV", "meV",
                            "W", "mW", "µW", "pW", "V", "mV", "A", "nA", "F", "fF", "H", "nH", "Ω", "kΩ",
                            "Wb", "Pa", "mbar", "bar", "mol/s", "mmol/s", "m", "mm", "µm", "kg", "u", "rad", "deg", "%", "dBm", "dB"}) {
        const UnitDef* u = c.find(sym);
        REQUIRE(u != nullptr);
        double v = 3.14159;
        auto j = c.toJson(c.fromUnit(v, sym).value(), sym);
        REQUIRE(j["unit"] == sym);
        REQUIRE(j["value"].get<double>() == Approx(v).epsilon(1e-12));
        auto back = c.fromJson(j, u->dim);
        REQUIRE(back.has_value());
        REQUIRE(*back == Approx(c.fromUnit(v, sym).value()).epsilon(1e-15));
    }
    auto q = c.fromJson<FrequencyDim>(core::Json{{"value", 4.812}, {"unit", "GHz"}});
    REQUIRE(q->v == Approx(4.812e9));
    REQUIRE_FALSE(c.fromJson<TimeDim>(core::Json{{"value", 4.812}, {"unit", "GHz"}}).has_value());
    REQUIRE_FALSE(c.fromJson<TimeDim>(core::Json{{"value", 1.0}, {"unit", "parsec"}}).has_value());
}

TEST_CASE("angles format in degrees and parse from degrees or radians") {
    using namespace qlab::units;
    const UnitCatalog& cat = UnitCatalog::global();
    const UnitDef* rad = cat.find("rad");
    REQUIRE(rad != nullptr);
    CHECK(cat.format(std::numbers::pi / 2.0, rad->dim, 3) == "90 °");
    CHECK(cat.format(std::numbers::pi / 6.0, rad->dim, 3) == "30 °");
    CHECK(cat.format(0.0, rad->dim, 3) == "0 °");
    CHECK(cat.format(-std::numbers::pi, rad->dim, 4) == "-180 °");
    const auto deg = cat.toUnit(std::numbers::pi / 4.0, "deg");
    REQUIRE(deg.has_value());
    CHECK(std::abs(*deg - 45.0) < 1e-12);
}
