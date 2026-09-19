// Spec 21 §4 — the views' ImGui code runs headless: header, body, hover card after 150 ms, click →
// shared selection, keys. No GL context: GL views must degrade to a placeholder, not crash.
#include "ImGuiHarness.hpp"
#include "Viz/Views/BlochView.hpp"
#include "Viz/Views/HintonView.hpp"
#include "Viz/Views/QSphereView.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace qlab;
using namespace qlab::viz;
using Catch::Approx;
using num::Complex;

namespace {
const double kInvSqrt2 = 1.0 / std::sqrt(2.0);

ViewInput bell() {
    auto s = std::make_shared<qsim::Snapshot>();
    s->nQubits = 2;
    s->gateIndex = 2;
    s->amplitudes = std::vector<Complex>{kInvSqrt2, 0.0, 0.0, Complex(0.0, kInvSqrt2)};
    ViewInput in;
    in.snapshot = std::move(s);
    return in;
}
} // namespace

TEST_CASE("Hinton view: model, hit test, CSV, and a headless ImGui frame with hover and click") {
    HintonView view;
    view.update(bell());
    REQUIRE(view.model().dim == 4);
    REQUIRE(view.model().squares.size() ==
            4); // rho_00, rho_03, rho_30, rho_33 of (|00> + i|11>)/sqrt2
    CHECK(
        view.wants(view.input()).empty()); // whole register, <= 8 qubits: nothing asked of the run

    const VizTheme theme = VizTheme::fallbackDark();
    SelectionModel selection;
    DrawContext ctx;
    ctx.theme = &theme;
    ctx.selection = &selection;
    ctx.showHeader = false;
    test::ImGuiHarness ui;
    ui.frame(view, ctx);
    CHECK(ui.vertices() > 50); // squares, grid, labels, phase wheel were emitted
    CHECK(view.bodySize().x > 100.0f);
    CHECK(view.cellSize() > 10.0f);

    // Element (0, 3) = -i/2: |rho| = 0.5, arg = -pi/2.
    const Rect cell = view.cellRect(0, 3);
    const glm::vec2 local(static_cast<float>(cell.cx()), static_cast<float>(cell.cy()));
    auto hit = view.hitTest(local);
    REQUIRE(hit.has_value());
    CHECK(hit->kind == HitKind::MatrixElement);
    CHECK(hit->element == std::optional<std::pair<std::uint32_t, std::uint32_t>>{{0u, 3u}});
    CHECK(hit->title == "rho[00, 11]");
    CHECK(hit->readout[1].value == "0.5");
    CHECK(hit->readout[2].value == "-π/2");
    CHECK_FALSE(view.hitTest({2.0f, 2.0f}).has_value());

    // Hover: the callback fires when the mouse lands on the element; the card waits 150 ms (spec 21
    // §4).
    int hovers = 0;
    view.setOnHover(
        [&](const HitResult& h) { hovers += h.kind == HitKind::MatrixElement ? 1 : 0; });
    const ImVec2 o = ui.bodyOrigin();
    ui.mouseTo(ImVec2(o.x + local.x, o.y + local.y));
    ui.frame(view, ctx);
    ui.frame(view, ctx, 0.2);
    CHECK(hovers == 1); // once per mark, not once per frame

    const auto csv = view.exportCsv();
    REQUIRE(csv.has_value());
    CHECK(csv->rfind("row,col,re,im\n", 0) == 0);
    CHECK(csv->find("00,11,0,-0.5") != std::string::npos);

    // With the header: badges, status line, export and "?" draw without an ImGui assertion.
    ctx.showHeader = true;
    std::string opened;
    ctx.openTheory = [&](std::string_view a) { opened = std::string(a); };
    ui.frame(view, ctx);
    CHECK(view.bodySize().y < ui.windowSize().y); // the header took its row
}

TEST_CASE("GL views draw a placeholder without a GL backend; a click still selects") {
    const VizTheme theme = VizTheme::fallbackDark();
    SelectionModel selection;
    DrawContext ctx;
    ctx.theme = &theme;
    ctx.selection = &selection;
    ctx.showHeader = false;
    ctx.gl = nullptr; // headless
    test::ImGuiHarness ui;

    BlochView bloch;
    bloch.update(bell());
    ui.frame(bloch, ctx);
    REQUIRE(bloch.cells().size() == 2);
    REQUIRE(bloch.cells()[1].onPage); // laid out even though nothing was rendered
    const ImVec2 o = ui.bodyOrigin();
    const glm::vec2 c = bloch.cells()[1].centerPx;
    ui.mouseTo(ImVec2(o.x + c.x, o.y + c.y));
    ui.frame(bloch, ctx);
    ui.mouseButton(true);
    ui.frame(bloch, ctx);
    ui.mouseButton(false);
    ui.frame(bloch, ctx);
    CHECK(selection.isSelected(QubitIndex{1})); // click qubit → shared selection (spec 21 §1.1)
    CHECK(selection.hoveredQubit() == std::optional<QubitIndex>{QubitIndex{1}});

    QSphereView qsphere;
    qsphere.update(bell());
    ui.frame(qsphere, ctx);
    CHECK(qsphere.model().nodes.size() == 2);
}
