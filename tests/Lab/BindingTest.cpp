// Spec 17 §5 — binding paths, placeholder substitution and provider resolution.
#include "Lab/BindingRegistry.hpp"
#include "Data/Fidelity.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qlab;
using namespace qlab::lab;

namespace {
InstanceParams attenuatorParams() {
    InstanceParams p;
    p.setIndex("line", 3);
    p.setIndex("k", 0);
    p.setToken("stage", "mxc");
    p.setNumber("A_dB", 20.0);
    return p;
}

SpecRow row(std::string field, std::string unit, std::string binding, data::FidelityClass cls) {
    SpecRow r;
    r.field = std::move(field);
    r.unit = std::move(unit);
    r.binding = std::move(binding);
    r.cls = cls;
    r.classDeclared = true;
    r.simulatorOnly = isSimulatorOnlyPath(*r.binding);
    return r;
}
} // namespace

TEST_CASE("placeholders substitute from the node instance") {
    auto p = attenuatorParams();
    CHECK(substitutePath("wiring.line[$line].attn[$k].P_diss", p).value() == "wiring.line[3].attn[0].P_diss");
    CHECK(substitutePath("cryo.stage.$stage.T", p).value() == "cryo.stage.mxc.T");
    CHECK(substitutePath("static.A_dB", p).value() == "static.A_dB");
    // a placeholder without an instance value leaves the row empty ("—")
    CHECK_FALSE(substitutePath("device.qubit[$i].f01", p).has_value());
    CHECK_FALSE(substitutePath("wiring.line[$].x", p).has_value());
    CHECK(p.index("line").value() == 3);
    CHECK_FALSE(p.index("stage").has_value());
}

TEST_CASE("binding roots split and round-trip") {
    for (auto [name, root] : {std::pair{"cryo", BindingRoot::Cryo}, {"wiring", BindingRoot::Wiring},
                              {"device", BindingRoot::Device}, {"instr", BindingRoot::Instr},
                              {"static", BindingRoot::Static}, {"run", BindingRoot::Run}}) {
        CHECK(bindingRootName(root) == name);
        CHECK(bindingRootFromName(name).value() == root);
        auto split = splitBindingRoot(std::string(name) + ".a.b[2]");
        REQUIRE(split.has_value());
        CHECK(split->first == root);
        CHECK(split->second == "a.b[2]");
    }
    auto bad = splitBindingRoot("nope.x");
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == kErrBinding);
    CHECK_FALSE(splitBindingRoot("cryo").has_value());
    // spec 00 §6 probe-only paths
    CHECK(isSimulatorOnlyPath("device.qubit[3].bloch"));
    CHECK(isSimulatorOnlyPath("device.ion[0].bloch[2]"));
    CHECK(isSimulatorOnlyPath("device.qubit[3].purity"));
    CHECK_FALSE(isSimulatorOnlyPath("device.qubit[3].pop_e"));
    CHECK_FALSE(isSimulatorOnlyPath("device.qubit[3].blochlike"));
}

TEST_CASE("the registry resolves a row through a fake provider") {
    BindingRegistry reg;
    CHECK_FALSE(reg.hasProvider(BindingRoot::Wiring));
    auto params = attenuatorParams();
    SpecRow diss = row("dissipated power", "W", "wiring.line[$line].attn[$k].P_diss", data::FidelityClass::Model);
    // no provider yet: the row shows "—"
    CHECK_FALSE(reg.resolve(params, diss).has_value());
    CHECK(formatBindingValue(reg.resolve(params, diss), diss.unit) == "—");

    reg.registerProvider(BindingRoot::Wiring, [](std::string_view path) -> std::optional<BindingValue> {
        if (path == "line[3].attn[0].P_diss") return BindingValue::number(1.25e-9, {}, data::FidelityClass::Model);
        if (path == "line[3].attn[0].n_th") return BindingValue::number(3.6e-3, "", data::FidelityClass::Model);
        return std::nullopt;
    });
    REQUIRE(reg.hasProvider(BindingRoot::Wiring));
    auto v = reg.resolve(params, diss);
    REQUIRE(v.has_value());
    CHECK(v->asNumber() == Catch::Approx(1.25e-9));
    CHECK(v->unit == "W"); // the provider gave none: the spec row's unit is used
    CHECK(v->cls == data::FidelityClass::Model);
    CHECK_FALSE(v->simulatorOnly);
    CHECK(formatBindingValue(v, "W") == "1.25e-09 W");

    // an unresolved instance parameter, an unknown path and an unknown root all read "—"
    InstanceParams bare;
    CHECK_FALSE(reg.resolve(bare, diss).has_value());
    CHECK_FALSE(reg.resolve("wiring.line[9].attn[0].P_diss").has_value());
    CHECK_FALSE(reg.resolve("bogus.x").has_value());

    // Simulator-only and the weakest-class rule of spec 00 §5
    SpecRow bloch = row("Bloch vector", "", "device.qubit[$i].bloch[0]", data::FidelityClass::Exact);
    params.setIndex("i", 7);
    reg.registerProvider(BindingRoot::Device, [](std::string_view path) -> std::optional<BindingValue> {
        if (path == "qubit[7].bloch[0]") return BindingValue::number(0.42, {}, data::FidelityClass::Numerical);
        return std::nullopt;
    });
    auto b = reg.resolve(params, bloch);
    REQUIRE(b.has_value());
    CHECK(b->simulatorOnly);
    CHECK(b->cls == data::FidelityClass::Numerical); // Lindblad probe is weaker than the row's Exact
    CHECK(b->available());

    // a NaN reading (probe disabled) is "not available" so the overlay hides
    reg.registerProvider(BindingRoot::Run, [](std::string_view) {
        return std::optional<BindingValue>{BindingValue::number(std::numeric_limits<double>::quiet_NaN())};
    });
    auto nan = reg.resolve("run.schedule.line[0].t_s");
    REQUIRE(nan.has_value());
    CHECK_FALSE(nan->available());
    CHECK(formatBindingValue(nan) == "—");
}

TEST_CASE("static bindings come from the node first and the registry second") {
    BindingRegistry reg;
    StaticProvider statics;
    statics.setNumber("f_c", 8e9, "Hz");
    reg.registerProvider(BindingRoot::Static, statics.asProvider());
    auto params = attenuatorParams();
    SpecRow attn = row("attenuation", "dB", "static.A_dB", data::FidelityClass::Model);
    SpecRow cutoff = row("cutoff", "GHz", "static.f_c", data::FidelityClass::Model);
    auto a = reg.resolve(params, attn);
    REQUIRE(a.has_value());
    CHECK(a->asNumber() == 20.0); // instance number, not in the shared table
    CHECK(a->unit == "dB");
    auto c = reg.resolve(params, cutoff);
    REQUIRE(c.has_value());
    CHECK(c->asNumber() == Catch::Approx(8e9));
    CHECK(c->unit == "Hz"); // the provider's unit wins over the row's
    // values added after registration are visible through the copied provider
    statics.setNumber("length_m", 0.25, "m");
    CHECK(reg.resolve("static.length_m").value().asNumber() == Catch::Approx(0.25));
    CHECK_FALSE(reg.resolve("static.missing").has_value());
    // text values (e.g. the GHS state name) format verbatim
    statics.set("ghs_state", BindingValue::text("circulating"));
    CHECK(formatBindingValue(reg.resolve("static.ghs_state")) == "circulating");
}
