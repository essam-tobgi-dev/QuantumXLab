// Spec 21 §3.13 and §5 — the circuit-diagram view over the layout of CircuitLayoutTest: stage
// selection with fall-back, the routed 5-qubit GHZ on sc_fixed_5 (inserted SWAP, moments identical
// to ir::Circuit::layers()), camera/hit-test agreement, playhead dimming, CSV and SVG export, the
// minimap threshold, a headless ImGui frame and a GL render (SKIP without a context).
#include "Viz/Views/CircuitView.hpp"
#include "Data/Fidelity.hpp"
#include "Graphics/Window.hpp"
#include "ImGuiHarness.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <numbers>

using namespace qlab;
using namespace qlab::viz;
using Catch::Approx;

namespace {
constexpr double kPi = std::numbers::pi;

void gate(ir::Circuit& c, const char* name, std::vector<std::uint32_t> wires,
          std::vector<double> params = {}) {
    std::vector<ir::Wire> ws;
    for (auto w : wires)
        ws.emplace_back(w);
    auto g = ir::makeGate(name, std::move(ws), std::move(params));
    REQUIRE(g.has_value());
    c.add(*g);
}

// Program-level 5-qubit GHZ: q0 is the source of four CXs. No SWAP anywhere.
std::shared_ptr<ir::Circuit> sourceGhz5() {
    auto c = std::make_shared<ir::Circuit>();
    c->setQubitCount(5);
    c->addQubitRegister({"q", 0, 5, false});
    gate(*c, "h", {0});
    for (std::uint32_t t = 1; t < 5; ++t)
        gate(*c, "cx", {0, t});
    return c;
}

// The same program routed onto sc_fixed_5 (edges 0–1, 1–2, 1–3, 3–4): program q0 sits on physical
// $1, the hub. $4 is two hops away, so the router inserts one SWAP(3, 4) before the last CX.
std::shared_ptr<ir::Circuit> routedGhz5() {
    auto c = std::make_shared<ir::Circuit>();
    c->setQubitCount(5);
    c->setPhysical(true);
    c->setLayout({1, 0, 2, 3, 4});
    gate(*c, "h", {1});
    gate(*c, "cx", {1, 0});
    gate(*c, "cx", {1, 2});
    gate(*c, "cx", {1, 3});
    gate(*c, "swap", {3, 4});
    gate(*c, "cx", {1, 3});
    return c;
}

ViewInput ghzInput() {
    ViewInput in;
    in.circuits.stages[static_cast<std::size_t>(CircuitStage::Source)] = sourceGhz5();
    const auto routed = routedGhz5();
    in.circuits.stages[static_cast<std::size_t>(CircuitStage::Routed)] = routed;
    in.circuits.layout = routed->layout();
    return in;
}
} // namespace

TEST_CASE("routed GHZ-5 on sc_fixed_5: the inserted SWAP is marked and the moments are layers()") {
    CircuitView view;
    CHECK(view.id() == "circuit");
    CHECK(view.observability() == Observability::Physical); // a circuit is what a real machine runs
    CHECK(view.backend() == Backend::GlCanvas);
    const ViewInput in = ghzInput();
    view.update(in);
    view.setBodySize({900.0f, 420.0f});
    CHECK(view.shownStage() == CircuitStage::Routed); // the default stage, and it is present
    CHECK(view.fidelity(in) == data::FidelityClass::Exact);
    CHECK(view.wants(in).empty()); // the diagram asks nothing of the run

    const layout::CircuitLayout& lay = view.circuitLayout();
    const auto layers = in.circuits.at(CircuitStage::Routed)->layers();
    CHECK(lay.moments == layers.size());
    CHECK(lay.moments == 6);
    CHECK(lay.columns == 6);      // one node per moment: no sub-columns
    CHECK(lay.routingSwaps == 1); // spec 21 §5: the diagram shows it
    CHECK_FALSE(lay.firstOverlap().has_value());
    for (const layout::Glyph& g : lay.glyphs) {
        REQUIRE(g.moment < layers.size());
        CHECK(std::find(layers[g.moment].begin(), layers[g.moment].end(), g.node) !=
              layers[g.moment].end());
    }
    // Physical wires labelled with the layout map: program q0 starts on physical $1.
    REQUIRE(lay.rows.size() == 5);
    CHECK(lay.rows[1].physical == std::optional<std::uint32_t>{1});
    CHECK(lay.rows[1].virtualQubit == std::optional<std::uint32_t>{0});
    CHECK(view.statusLine().find("Routed") == 0);
    CHECK(view.statusLine().find("1 routing SWAPs") != std::string::npos);
    CHECK_FALSE(view.minimap()); // 6 columns, far below 200

    // Same view, source stage: virtual wires, no SWAP, the moments of the source DAG.
    view.setStage(CircuitStage::Source);
    CHECK(view.shownStage() == CircuitStage::Source);
    CHECK(view.circuitLayout().routingSwaps == 0);
    CHECK(view.circuitLayout().rows[0].name == "q[0]");
    CHECK_FALSE(view.circuitLayout().rows[0].physical.has_value());
    // A stage the compiler did not produce falls back to the most compiled one below it.
    view.setStage(CircuitStage::Scheduled);
    CHECK(view.shownStage() == CircuitStage::Routed);
}

TEST_CASE("circuit view: the camera fits the diagram and the hit test agrees with it") {
    CircuitView view;
    view.update(ghzInput());
    view.setBodySize({900.0f, 420.0f});
    const layout::CircuitLayout& lay = view.circuitLayout();
    CHECK(view.zoom() > CircuitView::kMinZoom);
    // Framing puts the whole diagram inside the body, wire labels included.
    const glm::vec2 topLeft = view.toBody({-1.25, 0.0}),
                    bottomRight = view.toBody({lay.width, lay.height});
    CHECK(topLeft.x >= 0.0f);
    CHECK(topLeft.y >= 0.0f);
    CHECK(bottomRight.x <= 900.0f);
    CHECK(bottomRight.y <= 420.0f);
    // toBody / toLayout are exact inverses: the picture and the hit test cannot drift apart.
    const glm::dvec2 round = view.toLayout(view.toBody({2.5, 3.5}));
    CHECK(round.x == Approx(2.5).margin(1e-6));
    CHECK(round.y == Approx(3.5).margin(1e-6));

    // Click the SWAP: a gate hit carrying its topological index and the routing note.
    const layout::Glyph* swap = nullptr;
    for (const layout::Glyph& g : lay.glyphs)
        if (g.kind == layout::GlyphKind::Swap)
            swap = &g;
    REQUIRE(swap != nullptr);
    auto hit = view.hitTest(view.toBody({swap->bounds.cx(), swap->bounds.cy()}));
    REQUIRE(hit.has_value());
    CHECK(hit->kind == HitKind::Gate);
    CHECK(hit->gate == std::optional<std::uint32_t>{4});
    bool routed = false;
    for (const ReadoutRow& r : hit->readout)
        routed = routed || r.value == "SWAP inserted by the router";
    CHECK(routed);

    // Click a wire in the label gutter: the shared selection takes the PHYSICAL qubit (spec 21
    // §1.1).
    SelectionModel selection;
    const glm::vec2 wire = view.toBody({-0.6, lay.rowY(1)});
    auto wireHit = view.click(wire, &selection);
    REQUIRE(wireHit.has_value());
    CHECK(wireHit->kind == HitKind::Qubit);
    CHECK(wireHit->qubit == std::optional<QubitIndex>{QubitIndex{1}});
    CHECK(wireHit->title == "q0 \xE2\x86\x92 $1");
    CHECK(selection.isSelected(QubitIndex{1}));
    CHECK_FALSE(view.hitTest(view.toBody({-4.0, -4.0})).has_value());

    // Zoom is clamped and panning moves the diagram by exactly the layout distance asked for.
    view.setZoom(1e6);
    CHECK(view.zoom() == Approx(CircuitView::kMaxZoom));
    const glm::dvec2 before = view.pan();
    view.setPan(before + glm::dvec2(2.0, 0.0));
    CHECK(view.toBody({2.0, 0.0}).x ==
          Approx(view.toBody({4.0, 0.0}).x - 2.0f * CircuitView::kMaxZoom));
    view.frameContent();
    CHECK(view.pan().x ==
          Approx(before.x).margin(1e-9)); // `F` frames the content again (spec 21 §4)
}

TEST_CASE("circuit view: the playhead dims executed gates, and CSV/SVG export the layout") {
    ViewInput in = ghzInput();
    in.hasPlayhead = true;
    in.playheadGate = 3; // the third CX
    CircuitView view;
    view.update(in);
    view.setBodySize({900.0f, 420.0f});
    REQUIRE(view.playhead() == std::optional<std::uint64_t>{3});
    const layout::CircuitLayout& lay = view.circuitLayout();
    const auto stateOf = [&](std::uint32_t topo) {
        for (const layout::Glyph& g : lay.glyphs)
            if (g.topoIndex == topo) {
                auto h = view.hitTest(view.toBody({g.bounds.cx(), g.bounds.cy()}));
                REQUIRE(h.has_value());
                for (const ReadoutRow& r : h->readout)
                    if (r.label == "playhead")
                        return r.value;
            }
        return std::string("?");
    };
    CHECK(stateOf(0) == "executed");
    CHECK(stateOf(3) == "current gate");
    CHECK(stateOf(5) == "pending");

    const auto csv = view.exportCsv();
    REQUIRE(csv.has_value());
    CHECK(csv->rfind(
              "gate,kind,label,params,targets,controls,moment,column,depth,start_ns,duration_ns\n",
              0) == 0);
    CHECK(csv->find("\nswap,") == std::string::npos); // the kind column is the enum, label follows
    CHECK(csv->find(",swap,") != std::string::npos);

    // Spec 23 §8: vector export with the UI font named and a fallback stack.
    const std::string svg = view.exportSvg();
    CHECK(svg.rfind("<svg xmlns=\"http://www.w3.org/2000/svg\"", 0) == 0);
    CHECK(svg.find("viewBox=") != std::string::npos);
    CHECK(svg.find("font-family=\"Inter,") != std::string::npos);
    CHECK(svg.find("q0 \xE2\x86\x92 $1") !=
          std::string::npos);                       // the wire labels are text, not paths
    CHECK(svg.find("routed") != std::string::npos); // the inserted SWAP is called out
    CHECK(svg.substr(svg.size() - 7) == "</svg>\n");
}

TEST_CASE("circuit view: timed layout, angle text, and the minimap above 200 columns") {
    auto scheduled = std::make_shared<ir::Circuit>();
    scheduled->setQubitCount(2);
    scheduled->setPhysical(true);
    gate(*scheduled, "sx", {0});
    gate(*scheduled, "rz", {0}, {kPi / 2});
    gate(*scheduled, "cx", {0, 1});
    scheduled->meta()["schedule"] = {{"start_ps", {0, 40000, 40000}},
                                     {"length_ps", {40000, 0, 300000}}};
    ViewInput in;
    in.circuits.stages[static_cast<std::size_t>(CircuitStage::Scheduled)] = scheduled;

    CircuitView view;
    view.setStage(CircuitStage::Scheduled);
    view.setTimed(true);
    view.update(in);
    view.setBodySize({900.0f, 300.0f});
    REQUIRE(view.circuitLayout().timed);
    CHECK(view.circuitLayout().durationNs == Approx(340.0));
    // Gate durations come from the calibration the scheduler used: a fitted model (spec 00 §5).
    CHECK(view.fidelity(in) == data::FidelityClass::Model);
    CHECK(view.statusLine().find("340 ns") != std::string::npos);
    const layout::Glyph* rz = nullptr;
    for (const layout::Glyph& g : view.circuitLayout().glyphs)
        if (g.label == "rz")
            rz = &g;
    REQUIRE(rz != nullptr);
    CHECK(rz->params == "π/2"); // spec 21 §3.13: multiples of π up to denominator 16
    auto hit = view.hitTest(view.toBody({rz->bounds.cx(), rz->bounds.cy()}));
    REQUIRE(hit.has_value());
    CHECK(hit->title == "rz(π/2)");

    // Asking for the timed layout of an unscheduled stage keeps the diagram, on the moment layout.
    ViewInput plain;
    plain.circuits.stages[static_cast<std::size_t>(CircuitStage::Source)] = sourceGhz5();
    CircuitView fallback;
    fallback.setTimed(true);
    fallback.update(plain);
    fallback.setBodySize({600.0f, 300.0f});
    CHECK_FALSE(fallback.circuitLayout().timed);
    CHECK(fallback.circuitLayout().moments == 5);

    // > 200 columns: the view offers a minimap (spec 21 §3.13).
    auto deep = std::make_shared<ir::Circuit>();
    deep->setQubitCount(1);
    deep->addQubitRegister({"q", 0, 1, false});
    for (int k = 0; k < 210; ++k)
        gate(*deep, "h", {0});
    ViewInput big;
    big.circuits.stages[static_cast<std::size_t>(CircuitStage::Source)] = deep;
    CircuitView wide;
    wide.update(big);
    wide.setBodySize({900.0f, 300.0f});
    CHECK(wide.circuitLayout().columns == 210);
    CHECK(wide.minimap());
    CHECK(wide.zoom() == Approx(CircuitView::kMinZoom)); // clamped: the minimap is why it exists
}

TEST_CASE("circuit view: headless ImGui frame without GL, and a GL render to PNG") {
    const VizTheme theme = VizTheme::fallbackDark();
    SelectionModel selection;
    DrawContext ctx;
    ctx.theme = &theme;
    ctx.selection = &selection;
    ctx.showHeader = false;
    test::ImGuiHarness ui;
    CircuitView view;
    view.update(ghzInput());
    ui.frame(view, ctx); // no GL backend: a placeholder, not a crash
    CHECK(view.bodySize().x > 100.0f);
    CHECK(ui.vertices() > 0);
    const glm::vec2 wire = view.toBody({-0.6, view.circuitLayout().rowY(3)});
    const ImVec2 o = ui.bodyOrigin();
    ui.mouseTo(ImVec2(o.x + wire.x, o.y + wire.y));
    ui.frame(view, ctx);
    ui.mouseButton(true);
    ui.frame(view, ctx);
    ui.mouseButton(false);
    ui.frame(view, ctx);
    CHECK(selection.isSelected(QubitIndex{3})); // clicking a wire selects it everywhere

    gfx::WindowDesc wd;
    wd.visible = false;
    wd.width = 320;
    wd.height = 240;
    wd.vsync = false;
    auto window = gfx::Window::create(wd);
    if (!window)
        SKIP("no GL context available");
    GlBackendDesc desc;
    desc.background = glm::vec3(theme.bgPanel);
    auto gl = GlBackend::create(desc);
    REQUIRE(gl.has_value());
    const auto png = std::filesystem::path(QXL_SOURCE_DIR) / "build" / "viz_circuit.png";
    std::filesystem::create_directories(png.parent_path());
    REQUIRE(view.renderPng(**gl, theme, 720, 360, png).has_value());
    CHECK(std::filesystem::file_size(png) > 3000);
}
