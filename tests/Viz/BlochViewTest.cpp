// Spec 21 §3.1, §4, §5 — Bloch view: model from a snapshot, trail, pagination, hit testing and
// selection (headless), and a headless render to PNG checked pixel by pixel (SKIP without GL).
#include "Viz/Views/BlochView.hpp"
#include "Data/Fidelity.hpp"
#include "Graphics/Window.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <stb/stb_image.h>

using namespace qlab;
using namespace qlab::viz;
using Catch::Approx;
using num::Complex;

namespace {
const double kInvSqrt2 = 1.0 / std::sqrt(2.0);

std::shared_ptr<const qsim::Snapshot> snapshotOf(std::vector<Complex> psi, std::uint64_t gate = 0) {
    auto s = std::make_shared<qsim::Snapshot>();
    s->kind = qsim::Kind::StateVector;
    s->nQubits =
        static_cast<std::uint32_t>(std::lround(std::log2(static_cast<double>(psi.size()))));
    s->gateIndex = gate;
    s->amplitudes = std::move(psi);
    return s;
}

ViewInput inputOf(std::vector<Complex> psi, std::uint64_t gate = 0) {
    ViewInput in;
    in.snapshot = snapshotOf(std::move(psi), gate);
    return in;
}

struct Image {
    int w = 0, h = 0;
    std::vector<unsigned char> px;
    std::array<int, 3> at(glm::vec2 p) const {
        const int x = std::clamp(static_cast<int>(std::lround(p.x)), 0, w - 1),
                  y = std::clamp(static_cast<int>(std::lround(p.y)), 0, h - 1);
        const std::size_t o = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                               static_cast<std::size_t>(x)) *
                              4;
        return {px[o], px[o + 1], px[o + 2]};
    }
};
Image loadPng(const std::filesystem::path& p) {
    Image img;
    int n = 0;
    unsigned char* data = stbi_load(p.string().c_str(), &img.w, &img.h, &n, 4);
    REQUIRE(data != nullptr);
    img.px.assign(data,
                  data + static_cast<std::size_t>(img.w) * static_cast<std::size_t>(img.h) * 4);
    stbi_image_free(data);
    return img;
}
int distance(const std::array<int, 3>& a, const glm::vec4& c) {
    return std::abs(a[0] - static_cast<int>(c.r * 255)) +
           std::abs(a[1] - static_cast<int>(c.g * 255)) +
           std::abs(a[2] - static_cast<int>(c.b * 255));
}
std::filesystem::path buildDir() {
    const std::filesystem::path dir = std::filesystem::path(QXL_SOURCE_DIR) / "build";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}
} // namespace

TEST_CASE("Bloch view: Bell pair has |r| = 0 on both qubits, product states sit on the sphere") {
    BlochView view;
    CHECK(view.id() == "bloch");
    CHECK(view.observability() == Observability::SimulatorOnly);
    CHECK(view.backend() == Backend::GlCanvas);
    view.update(inputOf({kInvSqrt2, 0.0, 0.0, kInvSqrt2}));
    REQUIRE(view.cells().size() == 2);
    for (const auto& c : view.cells()) {
        CHECK(c.valid);
        CHECK(c.r.norm() == Approx(0.0).margin(1e-12));
        CHECK(c.entropyBits == Approx(1.0).margin(1e-10));
    }
    // |+i⟩ on qubit 1, |1⟩ on qubit 0 (little-endian: index = q0 + 2·q1).
    view.update(inputOf({0.0, kInvSqrt2, 0.0, Complex(0.0, kInvSqrt2)}, 1));
    CHECK(view.cell(QubitIndex{0})->r.z == Approx(-1.0).margin(1e-12));
    CHECK(view.cell(QubitIndex{1})->r.y == Approx(1.0).margin(1e-12));
    CHECK(view.fidelity(view.input()) == data::FidelityClass::Exact);
    CHECK(view.statusLine() == "gate 1");
    CHECK(view.wants(view.input()).singles);
}

TEST_CASE(
    "Bloch view: update is free for an unchanged input; the trail keeps one point per snapshot") {
    BlochView view;
    view.setTrailLength(3);
    ViewInput in = inputOf({1.0, 0.0}, 0);
    view.update(in);
    view.update(in);
    view.update(in);
    CHECK(view.rebuildCount() == 1); // same immutable snapshot: nothing recomputed
    for (std::uint64_t g = 1; g <= 4; ++g) {
        const double th = 0.3 * static_cast<double>(g);
        view.update(inputOf({std::cos(th / 2), std::sin(th / 2)}, g)); // rotation about y
    }
    const auto* c = view.cell(QubitIndex{0});
    REQUIRE(c != nullptr);
    REQUIRE(c->trail.size() == 3); // capped at the trail length, oldest dropped
    CHECK(c->trail.back().x == Approx(std::sin(1.2)).margin(1e-12));
    CHECK(c->trail.front().x == Approx(std::sin(0.6)).margin(1e-12));
    view.update(inputOf({1.0, 0.0}, 0)); // rewind: a new run starts the trail over
    CHECK(view.cell(QubitIndex{0})->trail.size() == 1);
}

TEST_CASE("Bloch view: reductions from the run, stale flag, 8 per row with pages, subset") {
    auto snap = std::make_shared<qsim::Snapshot>();
    snap->nQubits = 27; // too large for amplitudes here: the run supplies ρ_k
    snap->gateIndex = 5;
    auto red = std::make_shared<Reductions>();
    red->gateIndex = 5;
    red->nQubits = 27;
    for (std::uint32_t q = 0; q < 27; ++q) {
        SingleReduction s;
        s.qubit = QubitIndex{q};
        s.rho = num::Matrix(2, 2);
        s.rho(q % 2, q % 2) = 1.0;
        s.bloch = {0.0, 0.0, q % 2 ? -1.0 : 1.0};
        red->singles.push_back(s);
    }
    ViewInput in;
    in.snapshot = snap;
    in.reductions = red;
    BlochView view;
    view.update(in);
    view.setBodySize({960.0f, 300.0f});
    REQUIRE(view.cells().size() == 27);
    CHECK_FALSE(view.stale());
    CHECK(view.pageCount() == 2); // 8 per row, two rows of ≥ 120 px fit: 16 per page
    std::size_t onPage = 0;
    for (const auto& c : view.cells())
        onPage += c.onPage ? 1 : 0;
    CHECK(onPage == 16);
    CHECK(view.cells()[8].rect.y0 > view.cells()[0].rect.y0); // second row
    CHECK(view.cells()[7].rect.x0 > view.cells()[6].rect.x0); // eighth column
    CHECK_FALSE(view.cells()[0].rect.overlaps(view.cells()[1].rect));
    view.setPage(1);
    CHECK(view.cells()[16].onPage);
    CHECK_FALSE(view.cells()[0].onPage);
    CHECK(view.cell(QubitIndex{3})->r.z == Approx(-1.0));

    auto newer = std::make_shared<qsim::Snapshot>(*snap);
    newer->gateIndex = 6; // the run moved on, the reductions have not arrived yet
    in.snapshot = newer;
    view.update(in);
    CHECK(view.stale());

    const std::vector<QubitIndex> subset{QubitIndex{4}, QubitIndex{9}};
    view.setQubitSubset(subset);
    REQUIRE(view.cells().size() == 2);
    CHECK(view.cells()[1].qubit == QubitIndex{9});
    CHECK(view.pageCount() == 1);
}

TEST_CASE("Bloch view: hover readout and click select the qubit in the shared model") {
    BlochView view;
    view.update(inputOf({0.0, kInvSqrt2, 0.0, kInvSqrt2})); // q0 = |1⟩, q1 = |+⟩
    view.setBodySize({600.0f, 300.0f});
    const auto& c1 = view.cells()[1];
    auto hit = view.hitTest(c1.centerPx);
    REQUIRE(hit.has_value());
    CHECK(hit->kind == HitKind::Qubit);
    CHECK(*hit->qubit == QubitIndex{1});
    CHECK(hit->title == "q1");
    REQUIRE(hit->readout.size() >= 6);
    CHECK(hit->readout[0].value == "(1, 0, 0)");
    CHECK(hit->readout[1].value == "1");   // |r|
    CHECK(hit->readout[3].value == "π/2"); // θ
    CHECK_FALSE(view.hitTest({-5.0f, -5.0f}).has_value());

    SelectionModel selection;
    const std::vector<ComponentId> pads{ComponentId{101}, ComponentId{102}};
    selection.setQubitComponents(pads);
    int clicks = 0;
    view.setOnClick([&](const HitResult&) { ++clicks; });
    REQUIRE(view.click(c1.centerPx, &selection).has_value());
    CHECK(clicks == 1);
    CHECK(selection.isSelected(QubitIndex{1}));
    CHECK(selection.component() ==
          ComponentId{102}); // the lab highlights the qubit's pad (spec 21 §1.1)
    // |0⟩ is up: the north pole projects above the sphere centre, |+⟩ (x) toward the viewer.
    const auto& c0 = view.cells()[0];
    double depth = 0.0;
    CHECK(view.projectPoint(c0, {0, 0, 1}).y < c0.centerPx.y);
    CHECK(view.projectPoint(c0, {0, 0, -1}).y > c0.centerPx.y);
    view.projectPoint(c0, {1, 0, 0}, &depth);
    CHECK(depth > 0.0);
}

TEST_CASE("Bloch view renders to PNG: the arrow points up for |0> and down for |1>") {
    gfx::WindowDesc wd;
    wd.visible = false;
    wd.width = 320;
    wd.height = 240;
    wd.vsync = false;
    auto window = gfx::Window::create(wd);
    if (!window)
        SKIP("no GL context available");
    const VizTheme theme = VizTheme::load("dark").value_or(VizTheme::fallbackDark());
    GlBackendDesc desc;
    desc.background = glm::vec3(theme.bgPanel);
    auto gl = GlBackend::create(desc);
    REQUIRE(gl.has_value());

    const int W = 640, H = 360;
    BlochView view;
    view.update(inputOf({1.0, 0.0, 0.0, 0.0})); // |00⟩
    const auto up = buildDir() / "viz_bloch.png";
    REQUIRE(view.renderPng(**gl, theme, W, H, up).has_value());
    const Image a = loadPng(up);
    REQUIRE(a.w == W);
    REQUIRE(a.h == H);
    const auto& cell = view.cells()[0];
    const glm::vec2 shaftUp = view.projectPoint(cell, {0, 0, 0.5}),
                    shaftDown = view.projectPoint(cell, {0, 0, -0.5});
    const glm::vec4 orange = theme.qubitColor(0);
    // The lit shaft is not the exact token colour, but it is orange: red > green > blue and far
    // from the panel.
    const auto onShaft = a.at(shaftUp);
    CHECK(onShaft[0] > onShaft[1]);
    CHECK(onShaft[1] > onShaft[2]);
    CHECK(distance(onShaft, orange) < distance(onShaft, theme.bgPanel));
    CHECK(distance(a.at(shaftDown), theme.bgPanel) <
          distance(a.at(shaftDown), orange));                // nothing but shell below
    CHECK(distance(a.at({2.0f, 2.0f}), theme.bgPanel) <= 6); // exact panel colour

    // |11⟩ in a fresh view (no trail): the arrows point down and the upper half of the axis is
    // empty.
    BlochView flippedView;
    flippedView.update(inputOf({0.0, 0.0, 0.0, 1.0}));
    const auto down = buildDir() / "viz_bloch_one.png";
    REQUIRE(flippedView.renderPng(**gl, theme, W, H, down).has_value());
    const Image b = loadPng(down);
    const auto flipped = b.at(shaftDown);
    CHECK(flipped[0] > flipped[1]);
    CHECK(flipped[1] > flipped[2]);
    CHECK(distance(b.at(shaftUp), theme.bgPanel) < distance(b.at(shaftUp), orange));

    // The same state reached from |00⟩ leaves a trail from the north to the south pole: the upper
    // half of the z axis now carries the qubit colour (spec 21 §3.1 fading polyline).
    view.update(inputOf({0.0, 0.0, 0.0, 1.0}, 1));
    REQUIRE(view.cells()[0].trail.size() == 2);
    const auto trailPng = buildDir() / "viz_bloch_trail.png";
    REQUIRE(view.renderPng(**gl, theme, W, H, trailPng).has_value());
    const Image t = loadPng(trailPng);
    CHECK(distance(t.at(shaftUp), orange) < distance(t.at(shaftUp), theme.bgPanel));
    // Wireframe: the equator's front point (quantum −y·sin… use +x, nearest the viewer) is drawn.
    const glm::vec2 equatorFront = view.projectPoint(view.cells()[0], {1, 0, 0});
    CHECK(distance(a.at(equatorFront), theme.bgPanel) > 40);
    CHECK(std::filesystem::file_size(up) > 5000);
}
