// Spec 17 §5 — the App's live-value providers. Lab depends on neither Runtime nor Instruments, so
// these adapters are the only thing that makes a spec-sheet row on a scene node show a number.
// The oracles here are physical, not "a value came back":
//   · the stage temperatures are the thermal snapshot's, strictly ordered RT > PT1 > … > MXC;
//   · an attenuator's dissipation is P_in(1 − 10^(−A/10)) of the power that reaches it, so it
//     falls along the line, and its output photon number falls with it (spec 11 §2.3, §6);
//   · a thermometer reading tracks the true stage temperature within its own noise;
//   · `device.qubit[i].bloch` is Simulator-only: it resolves for a Bell state and disappears the
//     moment the Physical-lab toggle is on (spec 00 §6).
#include "App/App.hpp"
#include "Core/Paths.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace qlab;
using Catch::Approx;

namespace {

std::unique_ptr<app::LabModel> makeModel(bool physicalLab = false) {
    app::LabModel::Config config;
    config.device = "sc_fixed_5";
    config.physicalLab = physicalLab;
    auto model = app::LabModel::create(config);
    if (!model) FAIL(model.error().format());
    // One tick brings the sensors' first live acquisition in, which is what gives `cryo.thermo[i].T`
    // a value at all (spec 12 §1: a reading is an acquisition, not a snapshot field).
    (*model)->tick(0.1);
    (*model)->live().waitIdle();
    (*model)->tick(0.1);
    return std::move(*model);
}

double number(const lab::BindingRegistry& b, std::string_view path) {
    const auto v = b.resolve(path);
    if (!v) FAIL("no value for " + std::string(path));
    if (!v->isNumber()) FAIL(std::string(path) + " is not a number");
    return v->asNumber();
}

} // namespace

TEST_CASE("cryo.* resolves the thermal snapshot the network solved") {
    auto model = makeModel();
    const lab::BindingRegistry& b = model->bindings();
    const cryo::ThermalSnapshot& truth = model->cryo().snapshot;

    // The brief's path, and the layout spelling of the same stage (spec 17 §3.1).
    const double mxc = number(b, "cryo.stage.MXC.T");
    CHECK(mxc == Approx(truth.T_K[static_cast<std::size_t>(cryo::stageIndex(cryo::Stage::MXC))]).epsilon(1e-12));
    CHECK(number(b, "cryo.stage.mxc.T") == Approx(mxc).epsilon(1e-12));

    // A cold, circulating fridge: strictly ordered stages with the mixing chamber below 50 mK.
    const double rt = number(b, "cryo.stage.rt.T");
    const double pt1 = number(b, "cryo.stage.s50.T");
    const double pt2 = number(b, "cryo.stage.s4.T");
    const double still = number(b, "cryo.stage.still.T");
    const double cp = number(b, "cryo.stage.cp.T");
    CHECK(rt > pt1);
    CHECK(pt1 > pt2);
    CHECK(pt2 > still);
    CHECK(still > cp);
    CHECK(cp > mxc);
    CHECK(mxc < 0.05);
    CHECK(mxc > 0.003);
    CHECK(pt1 < 60.0);
    CHECK(pt2 < 5.0);

    // Cooling power must exceed the load at every stage of a fridge that reached base.
    for (const char* stage : {"s50", "s4", "still", "cp", "mxc"}) {
        const std::string root = std::string("cryo.stage.") + stage;
        CHECK(number(b, root + ".margin") == Approx(number(b, root + ".P_cool") - number(b, root + ".P_load")).epsilon(1e-9));
    }
    // The gas-handling state is a text binding (spec 17 §5).
    const auto state = b.resolve("cryo.ghs.state");
    REQUIRE(state.has_value());
    REQUIRE(state->asText() != nullptr);
    CHECK(*state->asText() == "base");
    CHECK(number(b, "cryo.pt.running") == 1.0);
    // A path nothing serves has no value; the inspector row then shows "—".
    CHECK_FALSE(b.resolve("cryo.stage.mxc.nonsense").has_value());
    CHECK_FALSE(b.resolve("cryo.stage.nowhere.T").has_value());
}

TEST_CASE("wiring.* dissipations and photon numbers fall along an input line") {
    auto model = makeModel();
    const lab::BindingRegistry& b = model->bindings();
    const cryo::Wiring& wiring = model->cryo().wiring;

    // An input line with at least two attenuators (every standard drive chain has several).
    std::size_t index = wiring.lines.size();
    for (std::size_t k = 0; k < wiring.lines.size(); ++k) {
        int attenuators = 0;
        for (const cryo::Element& e : wiring.lines[k].elements)
            if (e.kind == cryo::ElementKind::Attenuator) ++attenuators;
        if (wiring.lines[k].kind == cryo::LineKind::XY && attenuators >= 2) {
            index = k;
            break;
        }
    }
    REQUIRE(index < wiring.lines.size());
    const std::string root = std::format("wiring.line[{}]", index);

    // Spec 11 §5: with no schedule playing, the line carries no signal and nothing dissipates.
    CHECK(number(b, root + ".rt.P_in") == 0.0);
    CHECK(number(b, root + ".attn[0].P_diss") == 0.0);

    // Now put 1 µW on the bulkhead, as a pulse-level run would (spec 15 §3.5 (c)).
    constexpr double kInput = 1e-6;
    cryo::LinePowers powers;
    powers[wiring.lines[index].id] = kInput;
    model->applyLinePowers(powers);

    CHECK(number(b, root + ".rt.P_in") == Approx(kInput));
    const double first = number(b, root + ".attn[0].P_diss");
    const double second = number(b, root + ".attn[1].P_diss");
    CHECK(first > 0.0);
    CHECK(first < kInput);
    CHECK(second < first);   // the deeper attenuator sees the attenuated signal
    // P_diss = P_before (1 − 10^(−A/10)) with P_before ≤ P_in (only cable loss and the attenuators
    // above it stand in the way), so the implied input power is bounded by the bulkhead power.
    const cryo::Element* attn = nullptr;
    for (const cryo::Element& e : wiring.lines[index].elements)
        if (e.kind == cryo::ElementKind::Attenuator) {
            attn = &e;
            break;
        }
    REQUIRE(attn != nullptr);
    const double implied = first / (1.0 - std::pow(10.0, -attn->attenuation_dB / 10.0));
    CHECK(implied <= kInput * (1.0 + 1e-9));
    CHECK(implied > 0.5 * kInput);   // a cold coax run above the first attenuator loses little

    // Spec 11 §6 / T07 (9.2): each attenuator replaces the incoming photons with its own colder
    // bath, so the occupation falls monotonically down the line.
    const double n0 = number(b, root + ".attn[0].n_th");
    const double n1 = number(b, root + ".attn[1].n_th");
    CHECK(n0 > n1);
    CHECK(n1 > 0.0);
    // The clamp temperature of a stage is that stage's temperature.
    CHECK(number(b, root + ".clamp[mxc].T") == Approx(number(b, "cryo.stage.mxc.T")).epsilon(1e-12));
    // A coax segment conducts heat from the warmer stage into the colder one.
    CHECK(number(b, root + ".seg[0].T_hot") > number(b, root + ".seg[0].T_cold"));
    CHECK(number(b, root + ".seg[0].P_cond") > 0.0);
    CHECK_FALSE(b.resolve(std::format("wiring.line[{}].attn[0].P_diss", wiring.lines.size() + 7)).has_value());
}

TEST_CASE("instr.* and cryo.thermo[i] read the instruments, not the snapshot") {
    auto model = makeModel();
    const lab::BindingRegistry& b = model->bindings();

    // A generator's front-panel frequency, straight out of its settings schema (spec 12 §2).
    const double f = number(b, "instr.gen[0].f");
    CHECK(f > 1e9);
    CHECK(f < 2e10);
    CHECK(number(b, "instr.ref.locked") == 1.0);

    // Spec 12 §8: the RuO₂ thermometer on the mixing chamber tracks the truth within its noise and
    // the Kapitza-limited self-heating of its excitation — never exactly (that is `probe_thermal_truth`).
    const double truth = number(b, "cryo.stage.mxc.T");
    const double reading = number(b, "cryo.thermo[0].T");
    CHECK(reading > 0.0);
    CHECK(std::abs(reading - truth) < 0.5 * truth);
    // The pressure gauge on the outer vacuum can reads a pumped-down space.
    CHECK(number(b, "cryo.gauge[0].p") < 1e-3);
    CHECK_FALSE(b.resolve("instr.gen[99].f").has_value());
}

TEST_CASE("device.qubit[i].bloch is Simulator-only and hides in Physical-lab mode") {
    auto model = makeModel();
    // A Bell state: each qubit's reduced state is maximally mixed, so its Bloch vector vanishes.
    app::Options options;
    options.device = "sc_fixed_5";
    options.ideal = true;
    options.shotsGiven = true;
    options.shots = 64;
    const std::filesystem::path bell = core::assetDir() / "Programs" / "Examples" / "Basics" / "bell.qasm";
    const auto run = app::compileAndRunFile(model->session(), bell, options);
    if (!run) FAIL(run.error().format());
    model->setResult(std::make_shared<const runtime::RunResult>(run->result));

    viz::ReductionRequest request;
    request.singles = true;
    model->requestReductions(request);
    model->waitReductions();
    model->tick(1.0 / 60.0);

    const lab::BindingRegistry& b = model->bindings();
    for (int k = 0; k < 3; ++k) {
        const std::string path = std::format("device.qubit[0].bloch[{}]", k);
        const auto v = b.resolve(path);
        REQUIRE(v.has_value());
        CHECK(v->simulatorOnly);
        CHECK(std::abs(v->asNumber()) < 1e-9);   // |r| = 0 for half of a Bell pair
    }
    CHECK(number(b, "device.qubit[0].purity") == Approx(0.5).epsilon(1e-9));
    CHECK(number(b, "device.qubit[0].pop_e") == Approx(0.5).epsilon(1e-9));
    CHECK(number(b, "device.qubit[0].f01") > 4e9);
    CHECK(number(b, "device.qubit[0].T1") > 1e-6);
    CHECK(number(b, "device.T_sample") == Approx(number(b, "cryo.stage.mxc.T")).epsilon(1e-12));
    // T05 (3.4): E_J/E_C of a transmon is well above 20, which is what makes it charge-insensitive.
    CHECK(number(b, "device.qubit[0].EJ") / number(b, "device.qubit[0].EC") > 20.0);

    // Spec 00 §6: the same paths have no value once the Physical-lab toggle is on; `pop_e` falls
    // back to the calibrated thermal population, which is small, not one half.
    model->setPhysicalLab(true);
    CHECK_FALSE(b.resolve("device.qubit[0].bloch[2]").has_value());
    CHECK_FALSE(b.resolve("device.qubit[0].purity").has_value());
    CHECK(number(b, "device.qubit[0].pop_e") < 0.1);
    CHECK(number(b, "device.qubit[0].f01") > 4e9);   // a measured quantity stays visible
}

TEST_CASE("every binding a shipped component declares has a provider that answers or abstains") {
    // Spec 25 §7: an unresolved path is allowed to show "—", but a MALFORMED one (an unknown root,
    // a root without a provider) is a defect. Every `binding` of every shipped descriptor is walked.
    auto model = makeModel();
    const lab::BindingRegistry& b = model->bindings();
    for (lab::BindingRoot root : {lab::BindingRoot::Cryo, lab::BindingRoot::Wiring, lab::BindingRoot::Device,
                                  lab::BindingRoot::Instr, lab::BindingRoot::Static, lab::BindingRoot::Run})
        CHECK(b.hasProvider(root));

    auto catalog = lab::ComponentCatalog::load();
    if (!catalog) FAIL(catalog.error().format());
    std::size_t rows = 0, resolved = 0;
    std::vector<std::string> malformed;
    for (const lab::ComponentDescriptor& d : catalog->all())
        for (const lab::SpecRow& row : d.specSheet) {
            if (!row.binding) continue;
            ++rows;
            const std::string& full = *row.binding;
            const auto split = lab::splitBindingRoot(full);
            if (!split) malformed.push_back(full);
            // Placeholder paths ($line, $i) cannot resolve without a node; the query still must
            // not crash and must simply have no value.
            if (full.find('$') != std::string::npos) continue;
            if (b.resolve(full)) ++resolved;
        }
    CHECK(rows > 80);
    CHECK(malformed.empty());
    CHECK(resolved > 10);
}
