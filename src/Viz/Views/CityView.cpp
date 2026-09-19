// Spec 21 §3.5 — density-matrix city plot (see CityView.hpp).
#include "Viz/Views/CityView.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Detail/ImGuiSupport.hpp"
#include "Viz/Math/Color.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Math/Phase.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace qlab::viz {
using namespace detail;

namespace {
gfx::SubmitFlags flat() {
    gfx::SubmitFlags f;
    f.castShadow = f.receiveShadow = false;
    f.noPick = true; // hit testing is done on the CPU against the camera projection
    return f;
}
constexpr double kBarFill = 0.8; // fraction of a cell the bar occupies
constexpr double kPlateau = 1.6; // world height of a full-scale bar
} // namespace

ReductionRequest CityView::wants(const ViewInput& in) const {
    ReductionRequest r;
    const std::vector<QubitIndex> subset = shownQubits(in.qubitCount());
    if (!qubitSubset().empty() && subset.size() <= math::kDensityPlotMaxQubits &&
        subset.size() < in.qubitCount())
        r.subsets.push_back(subset);
    return r;
}

void CityView::setQuantity(math::CityQuantity q) {
    if (quantity_ == q)
        return;
    quantity_ = q;
    markDirty();
}

void CityView::setRotation(double yaw, double pitch) {
    yaw_ = yaw;
    pitch_ = std::clamp(pitch, 0.08, 1.45);
    canvas_.invalidate();
}

void CityView::resetCamera() {
    yaw_ = -0.9;
    pitch_ = 0.62;
    canvas_.invalidate();
}

void CityView::rebuild(const ViewInput& in) {
    canvas_.invalidate();
    model_ = {};
    source_ = densityFor(in, qubitSubset());
    if (source_.needsReduction)
        setStale(true);
    if (!source_.rho)
        return;
    if (auto built = math::buildCity(*source_.rho, quantity_))
        model_ = std::move(*built);
}

void CityView::layout() {
    canvas_.invalidate();
    // One world unit per cell keeps the grid square; the camera frames it in `aimCamera`.
    cell_ = 1.0;
    heightScale_ = model_.maxAbs > 0.0 ? kPlateau / model_.maxAbs : 0.0;
}

glm::dvec3 CityView::barBase(std::uint32_t row, std::uint32_t col) const {
    const double d = static_cast<double>(model_.dim);
    // Rows run along +x, columns along +z, both centred on the origin; bars grow along +y.
    return {(static_cast<double>(row) - 0.5 * (d - 1.0)) * cell_, 0.0,
            (static_cast<double>(col) - 0.5 * (d - 1.0)) * cell_};
}

void CityView::aimCamera(glm::vec2 body) {
    gfx::Camera& cam = canvas_.camera();
    const double span = std::max(2.0, static_cast<double>(model_.dim) * cell_);
    const double dist = 1.9 * span;
    const glm::dvec3 eye{dist * std::cos(pitch_) * std::sin(yaw_), dist * std::sin(pitch_),
                         dist * std::cos(pitch_) * std::cos(yaw_)};
    cam.setOrtho(false);
    cam.setFov(38.0);
    cam.setClip(0.05 * span, 20.0 * span);
    cam.setAspect(body.y > 0.0f ? static_cast<double>(body.x) / static_cast<double>(body.y) : 1.0);
    cam.lookAt(eye, {0.0, 0.25 * kPlateau, 0.0});
}

std::optional<glm::vec2> CityView::projectBar(std::uint32_t row, std::uint32_t col) const {
    const glm::vec2 body = bodySize();
    if (body.x <= 0.0f || body.y <= 0.0f || model_.dim == 0)
        return std::nullopt;
    double height = 0.0;
    for (const math::CityBar& b : model_.bars)
        if (b.row == row && b.col == col)
            height = b.height * heightScale_;
    glm::dvec2 px;
    const glm::dvec3 top = barBase(row, col) + glm::dvec3(0.0, std::max(0.0, height), 0.0);
    if (!canvas_.camera().project(top, static_cast<int>(body.x), static_cast<int>(body.y), px))
        return std::nullopt;
    return glm::vec2(static_cast<float>(px.x), static_cast<float>(px.y));
}

std::optional<HitResult> CityView::hitTest(glm::vec2 local) const {
    if (model_.bars.empty())
        return std::nullopt;
    // Nearest projected bar top within a radius that shrinks as the grid grows.
    const float radius = std::max(
        6.0f, 0.5f * bodySize().x / static_cast<float>(std::max<std::size_t>(1, model_.dim)));
    const math::CityBar* best = nullptr;
    float bestD = radius;
    for (const math::CityBar& b : model_.bars) {
        const auto p = projectBar(b.row, b.col);
        if (!p)
            continue;
        const float d = glm::length(*p - local);
        if (d < bestD) {
            bestD = d;
            best = &b;
        }
    }
    if (!best)
        return std::nullopt;
    const std::size_t nbits = source_.qubits.size();
    HitResult h;
    h.kind = HitKind::MatrixElement;
    h.element = std::pair<std::uint32_t, std::uint32_t>{best->row, best->col};
    h.title =
        "rho[" + math::bitString(best->row, nbits) + ", " + math::bitString(best->col, nbits) + "]";
    const data::FidelityClass cls = input().stateClass();
    h.readout.push_back({"ρ", math::formatCartesian(best->value), cls});
    h.readout.push_back({"|ρ|", math::formatSig(std::abs(best->value)), cls});
    h.readout.push_back({"arg ρ", math::formatAngle(std::arg(best->value)), cls});
    return h;
}

std::optional<std::string> CityView::exportCsv() const {
    if (model_.bars.empty())
        return std::nullopt;
    const std::size_t nbits = source_.qubits.size();
    std::string csv = "row,col,re,im\n";
    for (const math::CityBar& b : model_.bars)
        csv += math::bitString(b.row, nbits) + "," + math::bitString(b.col, nbits) + "," +
               math::formatSig(b.value.real(), 12) + "," + math::formatSig(b.value.imag(), 12) +
               "\n";
    return csv;
}

std::string CityView::statusLine() const {
    if (model_.dim == 0)
        return StateView::statusLine();
    std::string s = std::to_string(model_.dim) + " × " + std::to_string(model_.dim);
    if (source_.reduced)
        s += " reduced (" + subsetCaption(source_.qubits) + ")";
    s += "   max |ρ| = " + math::formatSig(model_.maxAbs);
    return s;
}

GlCanvas::SceneFn CityView::scene(const VizTheme& theme, float pxScale) const {
    return [this, &theme, pxScale](gfx::Renderer& r, GlBackend& gl) {
        const double d = static_cast<double>(model_.dim);
        const float labelPx =
            std::clamp(220.0f / static_cast<float>(std::max<std::size_t>(4, model_.dim)), 8.0f,
                       14.0f) *
            pxScale;
        // Base plate and the diagonal, which spec 21 §3.5 asks to be highlighted.
        const double half = 0.5 * d * cell_;
        for (int k = 0; k <= static_cast<int>(d); ++k) {
            const double t = -half + k * cell_;
            const float a = 0.22f;
            r.linesNoDepth().segment({-half, 0.0, t}, {half, 0.0, t},
                                     GlBackend::exact(glm::vec3(theme.border), a), pxScale);
            r.linesNoDepth().segment({t, 0.0, -half}, {t, 0.0, half},
                                     GlBackend::exact(glm::vec3(theme.border), a), pxScale);
        }
        r.linesNoDepth().segment({-half, 0.001, -half}, {half, 0.001, half},
                                 GlBackend::exact(glm::vec3(theme.accent), 0.55f), 1.6f * pxScale);
        for (const math::CityBar& b : model_.bars) {
            const double h = b.height * heightScale_;
            const glm::dvec3 base = barBase(b.row, b.col);
            gfx::Material m;
            m.unlit = true; // the colormap/phase value must reach the screen exactly (spec 22 §4)
            m.baseColor = GlBackend::exact(b.color, b.diagonal ? 1.0f : 0.92f);
            // A negative bar hangs below the plate; the cube mesh has its base at y = 0.
            const glm::mat4 model =
                glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(base)),
                           glm::vec3(static_cast<float>(kBarFill * cell_), static_cast<float>(h),
                                     static_cast<float>(kBarFill * cell_)));
            r.submit(gl.box(), m, model, ComponentId{0}, flat());
            if (b.diagonal && d <= 16.0)
                r.text().label3D(base + glm::dvec3(0.0, std::max(0.0, h) + 0.12, 0.0),
                                 math::formatSig(std::abs(b.value), 2), labelPx,
                                 GlBackend::exact(theme.textSecondary),
                                 gfx::TextAnchor::BottomCenter);
        }
        // Ket (rows) and bra (columns) labels, as spec 21 §3.5 asks.
        if (d <= 32.0) {
            const std::size_t nbits = source_.qubits.size();
            for (std::uint32_t i = 0; i < model_.dim; ++i) {
                const glm::dvec3 rowAt = barBase(i, 0) - glm::dvec3(0.0, 0.0, cell_);
                const glm::dvec3 colAt = barBase(0, i) - glm::dvec3(cell_, 0.0, 0.0);
                r.text().label3D(rowAt, math::ketLabel(i, nbits, false, true), labelPx,
                                 GlBackend::exact(theme.textSecondary), gfx::TextAnchor::Center);
                r.text().label3D(colAt, math::braLabel(i, nbits, false, true), labelPx,
                                 GlBackend::exact(theme.textSecondary), gfx::TextAnchor::Center);
            }
        }
    };
}

void CityView::drawBody(DrawContext& ctx) {
    if (model_.bars.empty()) {
        const std::string& n = source_.note;
        return widgets::placeholder(ctx, n.empty() ? "The density matrix is empty" : n);
    }
    if (!ctx.gl)
        return widgets::placeholder(ctx, "The city plot needs the GL canvas (no GL context)");
    const glm::vec2 body = bodySize();
    aimCamera(body);
    const float scale = std::max(1.0f, ctx.dpiScale);
    auto rendered =
        canvas_.render(*ctx.gl, static_cast<int>(body.x * scale), static_cast<int>(body.y * scale),
                       scene(*ctx.theme, scale), ctx.timeS);
    if (!rendered || !canvas_.hasImage())
        return widgets::placeholder(ctx, "GL canvas unavailable");
    ImGui::Image(static_cast<ImTextureID>(canvas_.textureId()), ImVec2(body.x, body.y),
                 ImVec2(0, 1), ImVec2(1, 0));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        setRotation(yaw_ - 0.008 * static_cast<double>(d.x),
                    pitch_ + 0.008 * static_cast<double>(d.y));
    }
    if (quantity_ == math::CityQuantity::Phase) {
        const ImVec2 o = ImGui::GetItemRectMin();
        widgets::phaseWheel(ctx, {o.x + body.x - 28.0f, o.y + 28.0f}, 18.0f);
    }
}

Status CityView::renderPng(GlBackend& gl, const VizTheme& theme, int widthPx, int heightPx,
                           const std::filesystem::path& png) {
    setBodySize({static_cast<float>(widthPx), static_cast<float>(heightPx)});
    aimCamera(bodySize());
    return canvas_.renderToPng(gl, widthPx, heightPx, scene(theme, 1.0f), png);
}

} // namespace qlab::viz
