// Spec 21 §3.1 — Bloch view: the GL scene (through gfx::Renderer) and the ImGui body.
#include "Viz/Detail/ImGuiSupport.hpp"
#include "Viz/Math/Color.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Views/BlochView.hpp"
#include "Viz/Widgets.hpp"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>

namespace qlab::viz {
namespace {

constexpr double kPi = std::numbers::pi;

// Every line goes through the renderer's overlay batch (`linesNoDepth`): its depth-tested batch
// draws nothing (measured: a 12 px segment through `lines()` leaves the background untouched in
// both projections, the same segment through `linesNoDepth()` is drawn — the batch is submitted
// while back-face culling is still enabled). The overlay batch is alpha-blended, so the far
// hemisphere is simply fainter; front/back is decided per segment on the CPU.
glm::vec4 ink(const glm::vec4& color, float alpha) {
    return GlBackend::exact(glm::vec3(color), alpha);
}

gfx::SubmitFlags flat() {
    gfx::SubmitFlags f;
    f.castShadow = f.receiveShadow = false;
    f.noPick = true; // hit testing is done on the CPU (BlochView::hitTest)
    return f;
}

} // namespace

void BlochView::drawCell(gfx::Renderer& r, GlBackend& gl, const VizTheme& theme, const Cell& c,
                         bool selected, float pxScale, const glm::dvec3& center) const {
    const glm::dmat3 rot = rotation();
    const auto world = [&](const glm::dvec3& q) { return center + rot * quantumToWorld(q); };
    const glm::vec4 grid = selected ? theme.accent : theme.textSecondary;

    // Shell: translucent, drawn after the depth-tested lines by the renderer's transparent pass.
    gfx::Material shell;
    shell.unlit = true;
    shell.baseColor = GlBackend::exact(glm::vec3(selected ? theme.accent : theme.textSecondary),
                                       selected ? 0.16f : 0.09f);
    r.submit(gl.sphere(), shell, glm::translate(glm::mat4(1.0f), glm::vec3(center)), ComponentId{0},
             flat());

    // Latitude and longitude every 30°, the equator emphasised; the far hemisphere is fainter.
    const int seg = cells_.size() > 16 ? 36 : 60;
    const auto circle = [&](auto pointAt, float alphaFront, float widthPx) {
        glm::dvec3 prevQ = pointAt(0.0);
        for (int k = 1; k <= seg; ++k) {
            const glm::dvec3 q = pointAt(2.0 * kPi * k / seg);
            const glm::dvec3 a = world(prevQ), b = world(q);
            const bool front = 0.5 * ((a.z - center.z) + (b.z - center.z)) >= 0.0;
            r.linesNoDepth().segment(a, b, ink(grid, front ? alphaFront : 0.35f * alphaFront),
                                     widthPx * pxScale);
            prevQ = q;
        }
    };
    for (int lat = -60; lat <= 60; lat += 30) {
        const double z = std::sin(lat * kPi / 180.0), rr = std::cos(lat * kPi / 180.0);
        circle([&](double t) { return glm::dvec3(rr * std::cos(t), rr * std::sin(t), z); },
               lat == 0 ? 0.85f : 0.32f, lat == 0 ? 1.8f : 1.0f);
    }
    for (int lon = 0; lon < 180; lon += 30) {
        const double cl = std::cos(lon * kPi / 180.0), sl = std::sin(lon * kPi / 180.0);
        circle(
            [&](double t) { return glm::dvec3(std::sin(t) * cl, std::sin(t) * sl, std::cos(t)); },
            0.32f, 1.0f);
    }

    // Axes with the six cardinal kets (SDF text, spec 21 §3.1). ASCII kets: the SDF atlas is built
    // from Inter, which has no U+27E9, and stores the font's .notdef box under that code point.
    struct Axis {
        glm::dvec3 dir;
        const char* plus;
        const char* minus;
    };
    const Axis axes[3] = {
        {{0, 0, 1}, "|0>", "|1>"}, {{1, 0, 0}, "|+>", "|->"}, {{0, 1, 0}, "|+i>", "|-i>"}};
    const float labelPx = std::clamp(0.13f * c.radiusPx, 9.0f, 15.0f) * pxScale;
    for (const Axis& ax : axes) {
        r.linesNoDepth().segment(world(-ax.dir), world(ax.dir), ink(theme.textSecondary, 0.55f),
                                 1.0f * pxScale);
        r.text().label3D(world(ax.dir * 1.2), ax.plus, labelPx, GlBackend::exact(theme.textPrimary),
                         gfx::TextAnchor::Center);
        r.text().label3D(world(ax.dir * -1.2), ax.minus, labelPx,
                         GlBackend::exact(theme.textSecondary), gfx::TextAnchor::Center);
    }
    // Qubit label under the sphere.
    r.text().label3D(center + glm::dvec3(0.0, -1.42, 0.0), "q" + std::to_string(c.qubit.get()),
                     1.15f * labelPx, GlBackend::exact(selected ? theme.accent : theme.textPrimary),
                     gfx::TextAnchor::Center);
    if (!c.valid)
        return;

    const glm::vec4 hue = theme.qubitColor(c.qubit.get());
    const glm::dvec3 rq(c.r.x, c.r.y, c.r.z);
    const double len = c.r.norm();

    // Trail of the last N snapshots: a polyline fading toward the oldest point.
    if (c.trail.size() > 1) {
        const double n = static_cast<double>(c.trail.size() - 1);
        for (std::size_t k = 1; k < c.trail.size(); ++k) {
            const float age = static_cast<float>(static_cast<double>(k) / n); // 0 oldest … 1 newest
            r.linesNoDepth().segment(world(c.trail[k - 1]), world(c.trail[k]),
                                     ink(hue, 0.12f + 0.78f * age), 1.6f * pxScale);
        }
    }

    // The vector: shaft + head whose tip is exactly at r, so its length reads as |r|.
    gfx::Material arrow;
    arrow.baseColor = glm::vec4(glm::vec3(hue), 1.0f);
    arrow.roughness = 0.55f;
    if (len > 1e-6) {
        const glm::dvec3 tip = world(rq), base = world(glm::dvec3(0.0));
        const double head = std::min(0.22, 0.5 * len);
        const glm::dvec3 neck = base + (tip - base) * ((len - head) / len);
        r.submit(gl.cylinder(), arrow, alignY(base, neck, 0.028), ComponentId{0}, flat());
        r.submit(gl.cone(), arrow, alignY(neck, tip, 0.075), ComponentId{0}, flat());
    }
    gfx::Material hub = arrow;
    r.submit(gl.sphereLow(), hub,
             glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(world(glm::dvec3(0.0)))),
                        glm::vec3(0.04f)),
             ComponentId{0}, flat());

    // Mixedness: a translucent disc at the tip, perpendicular to r, radius ∝ 1 − |r|.
    const double mixed = std::clamp(1.0 - len, 0.0, 1.0);
    if (mixed > 1e-3) {
        const glm::dvec3 tip = world(rq);
        const glm::dvec3 normal =
            len > 1e-6 ? glm::normalize(rot * quantumToWorld(rq)) : glm::dvec3(0.0, 0.0, 1.0);
        gfx::Material disc;
        disc.unlit = true;
        disc.baseColor = GlBackend::exact(glm::vec3(hue), 0.38f);
        r.submit(gl.disc(), disc, alignY(tip, tip + normal, 0.5 * mixed), ComponentId{0}, flat());
    }

    // Projection onto the z axis: dashed drop-line from the tip, with p1 = (1 − r_z)/2 printed.
    const glm::dvec3 foot(0.0, 0.0, c.r.z);
    r.linesNoDepth().segment(world(rq), world(foot), ink(hue, 0.8f), 1.2f * pxScale,
                             5.0f * pxScale);
    r.text().label3D(world(foot), "p1 = " + math::formatSig(c.r.p1(), 3), 0.85f * labelPx,
                     GlBackend::exact(theme.textSecondary), gfx::TextAnchor::BottomLeft,
                     glm::vec2(6.0f * pxScale, -2.0f * pxScale));
}

GlCanvas::SceneFn BlochView::scene(const VizTheme& theme, const SelectionModel* selection,
                                   float pxScale) const {
    return [this, &theme, selection, pxScale](gfx::Renderer& r, GlBackend& gl) {
        const glm::vec2 body = bodySize();
        for (const Cell& c : cells_) {
            if (!c.onPage || c.radiusPx <= 0.0f)
                continue;
            const double worldPerPx =
                1.0 / static_cast<double>(c.radiusPx); // the unit sphere is radiusPx on screen
            const glm::dvec3 center((c.centerPx.x - 0.5 * body.x) * worldPerPx,
                                    -(c.centerPx.y - 0.5 * body.y) * worldPerPx, 0.0);
            drawCell(r, gl, theme, c, selection && selection->isSelected(c.qubit), pxScale, center);
        }
    };
}

namespace {
// Orthographic camera looking down −z at the grid: one world unit is one sphere radius, so body
// pixels map linearly to world x/y and the CPU projection of BlochView::projectPoint is exact.
void aimCamera(gfx::Camera& cam, glm::vec2 body, float radiusPx) {
    cam.setOrtho(true);
    cam.lookAt({0.0, 0.0, 10.0}, {0.0, 0.0, 0.0});
    cam.setClip(0.1, 40.0);
    gfx::Bookmark b = cam.bookmark();
    b.orthoHeight =
        radiusPx > 0.0f ? static_cast<double>(body.y) / static_cast<double>(radiusPx) : 2.0;
    b.ortho = true;
    cam.set(b);
}
} // namespace

void BlochView::drawBody(DrawContext& ctx) {
    if (cells_.empty())
        return widgets::placeholder(ctx, "Run a program to see the qubit states");
    if (!ctx.gl)
        return widgets::placeholder(ctx, "Bloch spheres need the GL canvas (no GL context)");
    const glm::vec2 body = bodySize();
    float radiusPx = 0.0f;
    for (const Cell& c : cells_)
        if (c.onPage)
            radiusPx = c.radiusPx;
    aimCamera(canvas_.camera(), body, radiusPx);
    const float scale = std::max(1.0f, ctx.dpiScale);
    auto rendered =
        canvas_.render(*ctx.gl, static_cast<int>(body.x * scale), static_cast<int>(body.y * scale),
                       scene(*ctx.theme, ctx.selection, scale), ctx.timeS);
    if (!rendered || !canvas_.hasImage())
        return widgets::placeholder(ctx, "GL canvas unavailable");

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(canvas_.textureId()), ImVec2(body.x, body.y),
                 ImVec2(0, 1), ImVec2(1, 0));
    // Drag rotates every sphere about its own centre (yaw/pitch), so the grid stays put.
    if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        setRotation(yaw_ + 0.01 * static_cast<double>(d.x),
                    pitch_ + 0.01 * static_cast<double>(d.y));
    }
    if (pages_ > 1) { // pager (8 per row, spec 21 §3.1)
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 6.0f, origin.y + 4.0f));
        if (ImGui::SmallButton("<") && page_ > 0)
            setPage(page_ - 1);
        ImGui::SameLine();
        detail::text(ctx.theme->textSecondary,
                     "page " + std::to_string(page_ + 1) + "/" + std::to_string(pages_));
        ImGui::SameLine();
        if (ImGui::SmallButton(">") && page_ + 1 < pages_)
            setPage(page_ + 1);
    }
}

Status BlochView::renderPng(GlBackend& gl, const VizTheme& theme, int widthPx, int heightPx,
                            const std::filesystem::path& png) {
    setBodySize({static_cast<float>(widthPx), static_cast<float>(heightPx)});
    float radiusPx = 0.0f;
    for (const Cell& c : cells_)
        if (c.onPage)
            radiusPx = c.radiusPx;
    aimCamera(canvas_.camera(), bodySize(), radiusPx);
    return canvas_.renderToPng(gl, widthPx, heightPx, scene(theme, nullptr, 1.0f), png);
}

} // namespace qlab::viz
