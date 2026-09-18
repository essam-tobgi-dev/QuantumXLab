// Spec 21 §3.4 — Q-sphere view: placement in the view, hover/click, top-k notice (headless) and a
// headless render to PNG whose node colours are checked against the phase LUT (SKIP without GL).
#include "Graphics/Window.hpp"
#include "Viz/Math/Phase.hpp"
#include "Viz/Views/QSphereView.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <stb/stb_image.h>

using namespace qlab;
using namespace qlab::viz;
using Catch::Approx;
using num::Complex;

namespace {
const double kInvSqrt2 = 1.0 / std::sqrt(2.0);

ViewInput inputOf(std::vector<Complex> psi) {
    auto s = std::make_shared<qsim::Snapshot>();
    s->nQubits = static_cast<std::uint32_t>(std::lround(std::log2(static_cast<double>(psi.size()))));
    s->amplitudes = std::move(psi);
    ViewInput in;
    in.snapshot = std::move(s);
    return in;
}

int distance(const unsigned char* px, glm::vec3 c) {
    return std::abs(px[0] - static_cast<int>(c.r * 255)) + std::abs(px[1] - static_cast<int>(c.g * 255)) + std::abs(px[2] - static_cast<int>(c.b * 255));
}
} // namespace

TEST_CASE("Q-sphere view: GHZ sits on the poles; hover reads the state, click focuses it") {
    std::vector<Complex> ghz(8, 0.0);
    ghz[0] = kInvSqrt2;
    ghz[7] = -kInvSqrt2; // relative phase π
    QSphereView view;
    CHECK(view.id() == "qsphere");
    CHECK(view.observability() == Observability::SimulatorOnly);
    view.update(inputOf(ghz));
    view.setBodySize({400.0f, 400.0f});
    REQUIRE(view.model().nodes.size() == 2);
    const glm::vec2 north = view.projectPoint({0, 0, 1}), south = view.projectPoint({0, 0, -1});
    CHECK(north.y < view.centerPx().y); // |000⟩ (weight 0) at the top
    CHECK(south.y > view.centerPx().y); // |111⟩ (weight 3) at the bottom

    auto hit = view.hitTest(south);
    REQUIRE(hit.has_value());
    CHECK(hit->kind == HitKind::BasisState);
    CHECK(*hit->basisIndex == 7);
    CHECK(hit->title == "|111>");
    CHECK(hit->readout[0].value == "0.5");
    CHECK(hit->readout[1].value == "π");      // the hue is paired with the phase in text (spec 21 §4)
    CHECK(hit->readout[2].value == "3");
    CHECK_FALSE(view.hitTest(view.centerPx()).has_value());

    SelectionModel selection;
    REQUIRE(view.click(south, &selection).has_value());
    CHECK(selection.basisFocus() == std::optional<std::uint64_t>{7}); // sets the amplitude-bar filter
    REQUIRE(view.click(south, &selection).has_value());
    CHECK_FALSE(selection.basisFocus().has_value());                  // second click releases it
    CHECK(view.wants(view.input()).empty());                          // ≤ 20 qubits: nothing asked of the run
}

TEST_CASE("Q-sphere view: above 12 qubits only the top-k nodes are shown, and the view says so") {
    std::vector<Complex> flat(std::size_t{1} << 13, 1.0 / std::sqrt(8192.0));
    flat[5] = 0.0; // keep it normalised enough for the check below; one state simply drops out
    QSphereView view;
    view.update(inputOf(flat));
    CHECK(view.model().topKOnly);
    CHECK(view.model().nodes.size() == math::kDefaultTopK);
    CHECK(view.statusLine().find("top 256 of 8192 states") != std::string::npos);
}

TEST_CASE("Q-sphere view renders to PNG with node colours from the phase LUT") {
    gfx::WindowDesc wd;
    wd.visible = false;
    wd.width = 320;
    wd.height = 240;
    wd.vsync = false;
    auto window = gfx::Window::create(wd);
    if (!window) SKIP("no GL context available");
    const VizTheme theme = VizTheme::load("dark").value_or(VizTheme::fallbackDark());
    GlBackendDesc desc;
    desc.background = glm::vec3(theme.bgPanel);
    auto gl = GlBackend::create(desc);
    REQUIRE(gl.has_value());

    std::vector<Complex> ghz(8, 0.0);
    ghz[0] = kInvSqrt2;
    ghz[7] = -kInvSqrt2;
    QSphereView view;
    view.update(inputOf(ghz));
    const auto png = std::filesystem::path(QXL_SOURCE_DIR) / "build" / "viz_qsphere.png";
    std::filesystem::create_directories(png.parent_path());
    REQUIRE(view.renderPng(**gl, theme, 480, 480, png).has_value());
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(png.string().c_str(), &w, &h, &n, 4);
    REQUIRE(px != nullptr);
    REQUIRE(w == 480);
    const auto at = [&](glm::vec2 p) { return px + (static_cast<std::size_t>(std::lround(p.y)) * 480 + static_cast<std::size_t>(std::lround(p.x))) * 4; };
    const glm::vec3 zero = math::phaseColor(0.0), pi = math::phaseColor(std::numbers::pi);
    // Node centres face the viewer, not the baked key light, so they are a shade of the hue: the
    // north node must be the φ = 0 hue, the south node the φ = π hue, not the other way round.
    const unsigned char* northPx = at(view.projectPoint({0, 0, 1}));
    const unsigned char* southPx = at(view.projectPoint({0, 0, -1}));
    CHECK(distance(northPx, zero) < distance(northPx, pi));
    CHECK(distance(southPx, pi) < distance(southPx, zero));
    CHECK(distance(at({3.0f, 3.0f}), glm::vec3(theme.bgPanel)) <= 6);
    stbi_image_free(px);
    CHECK(std::filesystem::file_size(png) > 3000);
}
