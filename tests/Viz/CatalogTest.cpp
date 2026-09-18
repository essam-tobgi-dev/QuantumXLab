// Spec 21 §3 — the view catalog: all seventeen views of §3.1–§3.17 exist, carry the observability
// and drawing backend the spec assigns them, merge their reduction requests, and draw headless with
// no data and with a full `ViewInput` (no GL context: the GL views must fall back to a placeholder).
#include "Hardware/Hardware.hpp"
#include "ImGuiHarness.hpp"
#include "Viz/Viz.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <map>
#include <set>

using namespace qlab;
using namespace qlab::viz;
using num::Complex;

namespace {
const double kInvSqrt2 = 1.0 / std::sqrt(2.0);

void gate(ir::Circuit& c, const char* name, std::vector<std::uint32_t> wires) {
    std::vector<ir::Wire> ws;
    for (auto w : wires) ws.emplace_back(w);
    auto g = ir::makeGate(name, std::move(ws), {});
    REQUIRE(g.has_value());
    c.add(*g);
}

// A Bell pair with the device, the compiled circuit and 1000 shots behind it: enough for every
// view in the catalog to have something to draw.
ViewInput fullInput() {
    ViewInput in;
    auto snap = std::make_shared<qsim::Snapshot>();
    snap->nQubits = 2;
    snap->gateIndex = 2;
    snap->amplitudes = std::vector<Complex>{kInvSqrt2, 0.0, 0.0, kInvSqrt2};
    in.snapshot = snap;

    auto counts = std::make_shared<data::Histogram>(2);
    counts->add(std::string("00"), 508);
    counts->add(std::string("11"), 492);
    in.counts = counts;
    in.idealProbabilities = std::make_shared<std::vector<double>>(std::vector<double>{0.5, 0.0, 0.0, 0.5});

    auto source = std::make_shared<ir::Circuit>();
    source->setQubitCount(2);
    source->addQubitRegister({"q", 0, 2, false});
    gate(*source, "h", {0});
    gate(*source, "cx", {0, 1});
    in.circuits.stages[static_cast<std::size_t>(CircuitStage::Source)] = source;
    auto routed = std::make_shared<ir::Circuit>();
    routed->setQubitCount(2);
    routed->setPhysical(true);
    routed->setLayout({0, 1});
    gate(*routed, "h", {0});
    gate(*routed, "cx", {0, 1});
    in.circuits.stages[static_cast<std::size_t>(CircuitStage::Routed)] = routed;
    in.circuits.layout = routed->layout();
    in.hasPlayhead = true;
    in.playheadGate = 1;
    return in;
}
} // namespace

TEST_CASE("the catalog holds every view of spec 21 §3.1–§3.17 with its backend and observability") {
    auto views = makeAllViews();
    REQUIRE(views.size() == 17);
    REQUIRE(viewIds().size() == views.size());

    // Spec 21 §1.2 (backend) and §3 (observability). Anything not listed as Physical shows the
    // Simulator-only badge and is hidden by the "Physical lab" preset (spec 19 §3).
    const std::map<std::string_view, Backend> kBackend{
        {"bloch", Backend::GlCanvas},      {"amplitudes", Backend::ImPlot},   {"phasedisk", Backend::DrawList},
        {"qsphere", Backend::GlCanvas},    {"city", Backend::GlCanvas},       {"hinton", Backend::DrawList},
        {"histogram", Backend::ImPlot},    {"entanglement", Backend::GlCanvas}, {"schmidt", Backend::ImPlot},
        {"reduced", Backend::DrawList},    {"pauli", Backend::Table},         {"coupling", Backend::GlCanvas},
        {"circuit", Backend::GlCanvas},    {"pulses", Backend::ImPlot},       {"populations", Backend::ImPlot},
        {"trajectories", Backend::ImPlot}, {"wigner", Backend::ImPlot}};
    const std::set<std::string_view> kPhysical{"histogram", "coupling", "circuit", "pulses"};

    std::set<std::string_view> seen;
    for (std::size_t k = 0; k < views.size(); ++k) {
        const IStateView& v = *views[k];
        INFO("view " << v.id());
        CHECK(v.id() == viewIds()[k]);
        CHECK(seen.insert(v.id()).second);              // ids are unique
        REQUIRE(kBackend.contains(v.id()));
        CHECK(v.backend() == kBackend.at(v.id()));
        CHECK(v.observability() == (kPhysical.contains(v.id()) ? Observability::Physical
                                                               : Observability::SimulatorOnly));
        CHECK_FALSE(v.title().empty());
        CHECK(v.theoryAnchor().starts_with("T"));       // the "?" link of the header (spec 21 §4)
    }
    CHECK(makeView("qsphere") != nullptr);
    CHECK(makeView("qsphere")->id() == "qsphere");
    CHECK(makeView("no-such-view") == nullptr);
}

TEST_CASE("every view draws headless with no data and with a full input, and merges its requests") {
    const VizTheme theme = VizTheme::fallbackDark();
    SelectionModel selection;
    DrawContext ctx;
    ctx.theme = &theme;
    ctx.selection = &selection;
    ctx.showHeader = true;
    ctx.gl = nullptr; // no GL context: the GL views must show a placeholder, not crash
    test::ImGuiHarness ui;

    auto views = makeAllViews();
    const ViewInput empty;
    for (auto& v : views) {
        INFO("view " << v->id());
        v->update(empty);
        // Before the first run there is no register, so no request may name a qubit or a cut: an
        // out-of-range qubit is an error in `computeReductions`, not a note.
        const ReductionRequest r = v->wants(empty);
        CHECK(r.qubits.empty());
        CHECK(r.subsets.empty());
        CHECK_FALSE(r.schmidtPartition.has_value());
        ui.frame(*v, ctx);
        CHECK_FALSE(v->stale());
        CHECK(v->statusLine().empty());
    }

    ViewInput in = fullInput();
    auto loaded = hw::loadShippedDevice("sc_fixed_5");
    REQUIRE(loaded.has_value());
    in.device = borrow(loaded->device);
    in.calibration = borrow(loaded->calibration);

    // The App merges what every open view asks of the run and serves it once (spec 21 §1).
    ReductionRequest merged;
    for (auto& v : views) merged.merge(v->wants(in));
    CHECK(merged.singles);
    CHECK(merged.pairs);                    // the entanglement graph asked for the pair reductions
    CHECK(merged.qubits.empty());           // "all qubits" absorbs the per-view restrictions
    // Two qubits: the Pauli sums and the Schmidt SVD stay on the UI thread (spec 21 §2.3).
    CHECK_FALSE(merged.singleQubitPaulis);
    CHECK_FALSE(merged.schmidtPartition.has_value());
    auto reductions = computeReductions(*in.snapshot, merged);
    REQUIRE(reductions.has_value());
    in.reductions = std::make_shared<const Reductions>(std::move(*reductions));
    CHECK_FALSE(in.reductionsStale());

    for (auto& v : views) {
        INFO("view " << v->id());
        v->update(in);
        ui.frame(*v, ctx);
        CHECK_FALSE(v->stale());            // the reductions belong to this snapshot
        CHECK(v->bodySize().x > 100.0f);
        if (const auto csv = v->exportCsv()) CHECK(csv->find('\n') != std::string::npos);
        v->frameContent();                  // `F` and `R` are no-ops on the non-GL views
        v->resetCamera();
    }

    // The reductions of a newer snapshot have not arrived yet: the views that need them say so.
    auto later = std::make_shared<qsim::Snapshot>(*in.snapshot);
    later->gateIndex = 7;
    in.snapshot = later;
    CHECK(in.reductionsStale());
    int stale = 0;
    for (auto& v : views) {
        v->update(in);
        stale += v->stale() ? 1 : 0;
    }
    CHECK(stale > 0);
}
