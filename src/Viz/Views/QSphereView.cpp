// Spec 21 §3.4 — Q-sphere view: model, projection, hit testing, GL scene and ImGui body.
#include "Viz/Views/QSphereView.hpp"
#include "Viz/Detail/ImGuiSupport.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Math/Phase.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>

namespace qlab::viz {
namespace {
constexpr double kPi = std::numbers::pi;
glm::vec4 ink(const glm::vec4& color, float alpha) {
    return GlBackend::exact(glm::vec3(color), alpha);
}
gfx::SubmitFlags flat() {
    gfx::SubmitFlags f;
    f.castShadow = f.receiveShadow = false;
    f.noPick = true;
    return f;
}
} // namespace

ReductionRequest QSphereView::wants(const ViewInput& in) const {
    ReductionRequest r;
    if (in.snapshot && in.snapshot->nQubits > math::kFullAmplitudeQubits)
        r.topAmplitudes = options_.topK;
    return r;
}

void QSphereView::setOptions(const math::QSphereOptions& o) {
    options_ = o;
    markDirty();
}

void QSphereView::rebuild(const ViewInput& in) {
    canvas_.invalidate();
    model_ = {};
    if (!in.snapshot)
        return;
    if (in.snapshot->amplitudes && !in.snapshot->amplitudes->empty())
        model_ = math::buildQSphere(*in.snapshot->amplitudes, options_);
    else if (in.reductions && in.reductions->topAmplitudes)
        model_ = math::buildQSphere(*in.reductions->topAmplitudes,
                                    options_); // the run's top-k above 20 qubits
}

void QSphereView::layout() {
    canvas_.invalidate();
    const glm::vec2 body = bodySize();
    center_ = 0.5f * body;
    radius_ = 0.36f * std::min(body.x, body.y);
}

void QSphereView::setRotation(double yaw, double pitch) {
    yaw_ = yaw;
    pitch_ = std::clamp(pitch, -1.5, 1.5);
    canvas_.invalidate();
}
void QSphereView::resetCamera() {
    setRotation(-0.45, 0.32);
}

glm::dmat3 QSphereView::rotation() const {
    const double cy = std::cos(yaw_), sy = std::sin(yaw_), cp = std::cos(pitch_),
                 sp = std::sin(pitch_);
    const glm::dmat3 ry(cy, 0.0, -sy, 0.0, 1.0, 0.0, sy, 0.0, cy);
    const glm::dmat3 rx(1.0, 0.0, 0.0, 0.0, cp, sp, 0.0, -sp, cp);
    return rx * ry;
}

glm::vec2 QSphereView::projectPoint(const glm::dvec3& p, double* depth) const {
    const glm::dvec3 w = rotation() * quantumToWorld(p);
    if (depth)
        *depth = w.z;
    return {center_.x + radius_ * static_cast<float>(w.x),
            center_.y - radius_ * static_cast<float>(w.y)};
}

std::string QSphereView::statusLine() const {
    std::string s = StateView::statusLine();
    if (model_.topKOnly) // spec 21 §3.4: "shows the top-k nodes only and says so"
        s += "  top " + std::to_string(model_.nodes.size()) + " of " +
             std::to_string(model_.totalStates) +
             " states, p = " + math::formatSig(model_.shownProbability);
    return s;
}

std::optional<HitResult> QSphereView::hitTest(glm::vec2 local) const {
    // Nearest-to-viewer node under the cursor wins; nodes have a minimum pick radius of 5 px.
    const math::QSphereNode* best = nullptr;
    double bestDepth = -2.0;
    for (const auto& node : model_.nodes) {
        double depth = 0.0;
        const glm::vec2 p = projectPoint(node.place.position, &depth);
        const float pick = std::max(5.0f, radius_ * static_cast<float>(node.radius));
        if (glm::length(local - p) <= pick && depth > bestDepth) {
            best = &node;
            bestDepth = depth;
        }
    }
    if (!best)
        return std::nullopt;
    HitResult hit;
    hit.kind = HitKind::BasisState;
    hit.basisIndex = best->state.index;
    hit.title = math::ketLabel(best->state.index, model_.nQubits, false, true);
    const FidelityClass cls = input().stateClass();
    hit.readout.push_back({"probability", math::formatSig(best->state.probability), cls});
    hit.readout.push_back({"phase", math::formatAngle(best->state.phase),
                           cls}); // hue is always paired with a label (§4)
    hit.readout.push_back({"Hamming weight", std::to_string(best->place.weight), cls});
    return hit;
}

GlCanvas::SceneFn QSphereView::scene(const VizTheme& theme, const SelectionModel* selection,
                                     float pxScale) const {
    return [this, &theme, selection, pxScale](gfx::Renderer& r, GlBackend& gl) {
        const glm::vec2 body = bodySize();
        if (radius_ <= 0.0f)
            return;
        const double worldPerPx = 1.0 / static_cast<double>(radius_);
        const glm::dvec3 center((center_.x - 0.5 * body.x) * worldPerPx,
                                -(center_.y - 0.5 * body.y) * worldPerPx, 0.0);
        const glm::dmat3 rot = rotation();
        const auto world = [&](const glm::dvec3& q) { return center + rot * quantumToWorld(q); };

        gfx::Material shell;
        shell.unlit = true;
        shell.baseColor = GlBackend::exact(glm::vec3(theme.textSecondary), 0.07f);
        r.submit(gl.sphere(), shell, glm::translate(glm::mat4(1.0f), glm::vec3(center)),
                 ComponentId{0}, flat());

        // One latitude ring per Hamming weight (the poles are points), plus the vertical axis.
        const std::uint32_t n = model_.nQubits;
        for (std::uint32_t w = 1; w < n; ++w) {
            const double z = 1.0 - 2.0 * w / n, rr = std::sqrt(std::max(0.0, 1.0 - z * z));
            glm::dvec3 prev = world({rr, 0.0, z});
            for (int k = 1; k <= 60; ++k) {
                const double t = 2.0 * kPi * k / 60;
                const glm::dvec3 cur = world({rr * std::cos(t), rr * std::sin(t), z});
                const bool front = 0.5 * (prev.z + cur.z) - center.z >= 0.0;
                r.linesNoDepth().segment(prev, cur, ink(theme.textSecondary, front ? 0.45f : 0.16f),
                                         1.0f * pxScale);
                prev = cur;
            }
        }
        r.linesNoDepth().segment(world({0, 0, -1}), world({0, 0, 1}),
                                 ink(theme.textSecondary, 0.35f), 1.0f * pxScale);

        const auto focus = selection ? selection->basisFocus() : std::nullopt;
        const float labelPx = std::clamp(0.06f * radius_, 10.0f, 14.0f) * pxScale;
        for (const auto& node : model_.nodes) {
            const glm::dvec3 p = world(node.place.position);
            if (node.spoke)
                r.linesNoDepth().segment(center, p, ink(glm::vec4(node.color, 1.0f), 0.75f),
                                         1.4f * pxScale);
            gfx::Material m;
            m.unlit =
                true; // baked shading × exact phase hue: the lit side matches the legend exactly
            m.baseColor = GlBackend::exact(node.color);
            const float rad = static_cast<float>(std::max(node.radius, 0.012));
            r.submit(gl.sphereLow(), m,
                     glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(p)), glm::vec3(rad)),
                     ComponentId{0}, flat());
            // Outline facing the viewer: with H = (φ + π)/2π the φ = 0 hue is the darkest entry of
            // the twilight LUT, which all but vanishes on the dark panel without a rim.
            r.linesNoDepth().circle(glm::vec3(p), glm::vec3(0.0f, 0.0f, 1.0f), rad,
                                    ink(theme.textSecondary, 0.9f), 32, 1.2f * pxScale);
            const bool focused = focus && *focus == node.state.index;
            // Label the states that carry weight (and the focused one); 4096 labels would be noise.
            if (focused || node.state.probability > 0.02)
                r.text().label3D(p, math::ketLabel(node.state.index, n, n > 6, true), labelPx,
                                 GlBackend::exact(focused ? theme.accent : theme.textPrimary),
                                 gfx::TextAnchor::BottomCenter,
                                 glm::vec2(0.0f, -(rad * radius_ + 3.0f) * pxScale));
        }
    };
}

void QSphereView::aimCamera() {
    gfx::Camera& cam = canvas_.camera();
    cam.lookAt({0.0, 0.0, 10.0}, {0.0, 0.0, 0.0});
    cam.setClip(0.1, 40.0);
    gfx::Bookmark b = cam.bookmark();
    b.ortho = true;
    b.orthoHeight =
        radius_ > 0.0f ? static_cast<double>(bodySize().y) / static_cast<double>(radius_) : 2.0;
    cam.set(b);
}

void QSphereView::drawBody(DrawContext& ctx) {
    if (model_.nodes.empty())
        return widgets::placeholder(ctx,
                                    "Run a program on a state-vector backend to see the Q-sphere");
    if (!ctx.gl)
        return widgets::placeholder(ctx, "The Q-sphere needs the GL canvas (no GL context)");
    const glm::vec2 body = bodySize();
    aimCamera();
    const float scale = std::max(1.0f, ctx.dpiScale);
    auto rendered =
        canvas_.render(*ctx.gl, static_cast<int>(body.x * scale), static_cast<int>(body.y * scale),
                       scene(*ctx.theme, ctx.selection, scale), ctx.timeS);
    if (!rendered || !canvas_.hasImage())
        return widgets::placeholder(ctx, "GL canvas unavailable");
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(canvas_.textureId()), ImVec2(body.x, body.y),
                 ImVec2(0, 1), ImVec2(1, 0));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        setRotation(yaw_ + 0.01 * static_cast<double>(d.x),
                    pitch_ + 0.01 * static_cast<double>(d.y));
    }
    // Phase legend: colour alone never carries the phase (spec 21 §4).
    widgets::phaseWheel(ctx, {origin.x + body.x - 58.0f, origin.y + 50.0f}, 24.0f);
}

Status QSphereView::renderPng(GlBackend& gl, const VizTheme& theme, int widthPx, int heightPx,
                              const std::filesystem::path& png) {
    setBodySize({static_cast<float>(widthPx), static_cast<float>(heightPx)});
    aimCamera();
    return canvas_.renderToPng(gl, widthPx, heightPx, scene(theme, nullptr, 1.0f), png);
}

} // namespace qlab::viz
