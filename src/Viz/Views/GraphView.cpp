// Spec 21 §3.12, §3.8 — shared geometry, GL scene and hit testing of the node-link views
// (see GraphView.hpp). The concrete views fill `graph_` and add their own overlay.
#include "Viz/Views/GraphView.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Detail/ImGuiSupport.hpp"
#include "Viz/Math/Color.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>

namespace qlab::viz {
using namespace detail;

namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr float kMargin = 26.0f;       // body pixels kept free around the graph
constexpr double kCouplerScale = 0.45; // a tunable coupler is drawn smaller than a data qubit

gfx::SubmitFlags flat() {
    gfx::SubmitFlags f;
    f.castShadow = f.receiveShadow = false;
    f.noPick = true; // hit testing is on the CPU, in layout units (DeviceGraph::nodeAt / edgeAt)
    return f;
}
} // namespace

void DeviceGraphView::layout() {
    canvas_.invalidate();
    const glm::vec2 body = bodySize();
    if (graph_.nodes.empty() || body.x <= 0.0f || body.y <= 0.0f) {
        pxPerUnit_ = 1.0;
        return;
    }
    const Rect& e = graph_.extent;
    center_ = {e.cx(), e.cy()};
    // One extra node radius on each side so that the outermost discs and their labels fit.
    const double w = std::max(1e-6, e.width() + 2.0 * nodeRadius_ * 2.0);
    const double h = std::max(1e-6, e.height() + 2.0 * nodeRadius_ * 2.0);
    pxPerUnit_ = std::min((body.x - 2.0f * kMargin) / w, (body.y - 2.0f * kMargin) / h);
    pxPerUnit_ = std::max(pxPerUnit_, 1.0);
}

glm::vec2 DeviceGraphView::toBody(glm::dvec2 p) const {
    const glm::vec2 body = bodySize();
    return {static_cast<float>(0.5 * body.x + (p.x - center_.x) * pxPerUnit_),
            static_cast<float>(0.5 * body.y + (p.y - center_.y) * pxPerUnit_)};
}

glm::dvec2 DeviceGraphView::toLayout(glm::vec2 p) const {
    const glm::vec2 body = bodySize();
    return {center_.x + (static_cast<double>(p.x) - 0.5 * body.x) / pxPerUnit_,
            center_.y + (static_cast<double>(p.y) - 0.5 * body.y) / pxPerUnit_};
}

std::optional<HitResult> DeviceGraphView::hitTest(glm::vec2 local) const {
    if (graph_.nodes.empty() || pxPerUnit_ <= 0.0) return std::nullopt;
    const glm::dvec2 p = toLayout(local);
    if (const auto k = graph_.nodeAt(p, nodeRadius_ * 1.15)) {
        const layout::GraphNode& n = graph_.nodes[*k];
        HitResult h;
        h.kind = HitKind::Qubit;
        h.qubit = n.qubit;
        h.title = "q" + std::to_string(n.qubit.get()) + (n.coupler ? "  (coupler)" : "");
        if (n.virtualQubit) h.readout.push_back({"program qubit", "q" + std::to_string(*n.virtualQubit), data::FidelityClass::Exact});
        fillNodeReadout(h, n);
        return h;
    }
    // Edges are picked with a tolerance of a third of a node radius, so a click near a node wins.
    if (const auto k = graph_.edgeAt(p, nodeRadius_ * 0.34)) {
        const layout::GraphEdge& e = graph_.edges[*k];
        HitResult h;
        h.kind = HitKind::Edge;
        h.edge = std::pair<QubitIndex, QubitIndex>{e.a, e.b};
        h.title = "q" + std::to_string(e.a.get()) + (e.directed ? " → q" : " – q") + std::to_string(e.b.get());
        if (e.coupler) h.readout.push_back({"coupler", "q" + std::to_string(e.coupler->get()), data::FidelityClass::Exact});
        fillEdgeReadout(h, e);
        return h;
    }
    return std::nullopt;
}

std::optional<std::string> DeviceGraphView::exportCsv() const {
    if (graph_.nodes.empty()) return std::nullopt;
    std::string csv = "kind,a,b,value,text\n";
    for (const layout::GraphNode& n : graph_.nodes)
        csv += std::string(n.coupler ? "coupler" : "qubit") + "," + std::to_string(n.qubit.get()) + ",," +
               (n.value ? math::formatSig(*n.value, 12) : std::string()) + "," + n.valueText + "\n";
    for (const layout::GraphEdge& e : graph_.edges)
        csv += "edge," + std::to_string(e.a.get()) + "," + std::to_string(e.b.get()) + "," +
               (e.value ? math::formatSig(*e.value, 12) : std::string()) + "," + e.valueText + "\n";
    return csv;
}

void DeviceGraphView::aimCamera(glm::vec2 body) {
    // Orthographic, looking down −z: one world unit is one body pixel divided by pxPerUnit_, so the
    // CPU mapping of `toBody` is exact. Device y grows down the page, the renderer's y grows up.
    gfx::Camera& cam = canvas_.camera();
    cam.setOrtho(true);
    cam.lookAt({0.0, 0.0, 10.0}, {0.0, 0.0, 0.0});
    cam.setClip(0.1, 40.0);
    cam.setAspect(body.y > 0.0f ? static_cast<double>(body.x) / static_cast<double>(body.y) : 1.0);
    gfx::Bookmark b = cam.bookmark();
    b.ortho = true;
    b.orthoHeight = pxPerUnit_ > 0.0 ? static_cast<double>(body.y) / pxPerUnit_ : 2.0;
    cam.set(b);
}

GlCanvas::SceneFn DeviceGraphView::scene(const VizTheme& theme, const SelectionModel* selection, float pxScale) const {
    return [this, &theme, selection, pxScale](gfx::Renderer& r, GlBackend& gl) {
        // World frame: x as in the device layout, y flipped (the layout's y goes down the page).
        const auto world = [this](glm::dvec2 p) { return glm::dvec3(p.x - center_.x, center_.y - p.y, 0.0); };
        const float labelPx = std::clamp(static_cast<float>(0.7 * nodeRadius_ * pxPerUnit_), 8.0f, 16.0f) * pxScale;

        for (const layout::GraphEdge& e : graph_.edges) {
            if (e.a.get() >= graph_.nodes.size() || e.b.get() >= graph_.nodes.size()) continue;
            const glm::dvec3 a = world(graph_.nodes[e.a.get()].pos), b = world(graph_.nodes[e.b.get()].pos);
            const bool picked = selection && selection->edge() &&
                                selection->edge()->first == e.a && selection->edge()->second == e.b;
            // Entanglement graph: width and opacity carry I(i:j)/2. Coupling graph: constant width.
            const float alpha = static_cast<float>(0.25 + 0.7 * std::clamp(e.weight, 0.0, 1.0));
            const float width = static_cast<float>(1.4 + 4.0 * std::clamp(e.weight, 0.0, 1.0)) * pxScale;
            r.linesNoDepth().segment(a, b, GlBackend::exact(picked ? glm::vec3(theme.accent) : e.color, picked ? 1.0f : alpha),
                                     picked ? width + 1.5f * pxScale : width);
            if (e.concurrence)
                r.text().label3D(0.5 * (a + b), "C = " + math::formatSig(*e.concurrence, 3), 0.85f * labelPx,
                                 GlBackend::exact(theme.textSecondary), gfx::TextAnchor::BottomCenter);
        }
        for (const layout::GraphNode& n : graph_.nodes) {
            const double radius = nodeRadius_ * (n.coupler ? kCouplerScale : 1.0);
            const bool picked = selection && selection->isSelected(n.qubit);
            gfx::Material m;
            m.unlit = true; // a colormap value must reach the screen exactly (spec 22 §4)
            m.baseColor = GlBackend::exact(n.color, 1.0f);
            const glm::dvec3 c = world(n.pos);
            r.submit(gl.sphereLow(), m, glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(c)), glm::vec3(radius)),
                     ComponentId{0}, flat());
            if (picked || n.syndromeFired) {
                const glm::vec4 ring = n.syndromeFired && !picked ? theme.warn : theme.accent;
                constexpr int kSeg = 28;
                for (int k = 0; k < kSeg; ++k) {
                    const double t0 = kTwoPi * k / kSeg, t1 = kTwoPi * (k + 1) / kSeg;
                    const double rr = radius * 1.28;
                    r.linesNoDepth().segment(c + glm::dvec3(rr * std::cos(t0), rr * std::sin(t0), 0.01),
                                             c + glm::dvec3(rr * std::cos(t1), rr * std::sin(t1), 0.01),
                                             GlBackend::exact(glm::vec3(ring), 1.0f), 2.0f * pxScale);
                }
            }
            if (n.coupler) continue;
            // Spec 21 §3.12: the mapped program qubit is printed inside the node, the index below it.
            const glm::vec4 ink = GlBackend::exact(math::readableOn(n.color, glm::vec3(1.0f), glm::vec3(0.0f)), 1.0f);
            if (n.virtualQubit)
                r.text().label3D(c, std::to_string(*n.virtualQubit), labelPx, ink, gfx::TextAnchor::Center);
            r.text().label3D(c - glm::dvec3(0.0, radius * 1.35, 0.0), "q" + std::to_string(n.qubit.get()), 0.85f * labelPx,
                             GlBackend::exact(picked ? glm::vec3(theme.accent) : glm::vec3(theme.textSecondary), 1.0f),
                             gfx::TextAnchor::TopCenter);
        }
        drawOverlay(r, gl, theme, pxScale);
    };
}

void DeviceGraphView::drawBody(DrawContext& ctx) {
    if (graph_.nodes.empty()) return widgets::placeholder(ctx, note_.empty() ? "No device loaded" : note_);
    if (!ctx.gl) return widgets::placeholder(ctx, "The graph needs the GL canvas (no GL context)");
    const glm::vec2 body = bodySize();
    aimCamera(body);
    const float scale = std::max(1.0f, ctx.dpiScale);
    auto rendered = canvas_.render(*ctx.gl, static_cast<int>(body.x * scale), static_cast<int>(body.y * scale),
                                   scene(*ctx.theme, ctx.selection, scale), ctx.timeS);
    if (!rendered || !canvas_.hasImage()) return widgets::placeholder(ctx, "GL canvas unavailable");
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(canvas_.textureId()), ImVec2(body.x, body.y), ImVec2(0, 1), ImVec2(1, 0));
    // Spec 22 §4: a colour-coded scalar always carries its min/max legend.
    if (showNodeLegend() && graph_.nodeLegend.valid) {
        const Rect bar{origin.x + 10.0, origin.y + 24.0, origin.x + 24.0, origin.y + std::min(180.0f, 0.6f * body.y)};
        widgets::colorBar(ctx, bar, [](double t) { return math::sequentialColor(t); }, graph_.nodeLegend.minLabel,
                          graph_.nodeLegend.maxLabel, graph_.nodeLegend.title);
    }
    if (graph_.edgeLegend.valid) {
        const float x = origin.x + body.x - 60.0f;
        const Rect bar{x, origin.y + 24.0, x + 14.0, origin.y + std::min(180.0f, 0.6f * body.y)};
        widgets::colorBar(ctx, bar, [](double t) { return math::sequentialColor(t); }, graph_.edgeLegend.minLabel,
                          graph_.edgeLegend.maxLabel, graph_.edgeLegend.title);
    }
}

Status DeviceGraphView::renderPng(GlBackend& gl, const VizTheme& theme, int widthPx, int heightPx,
                                  const std::filesystem::path& png) {
    setBodySize({static_cast<float>(widthPx), static_cast<float>(heightPx)});
    layout();
    aimCamera(bodySize());
    return canvas_.renderToPng(gl, widthPx, heightPx, scene(theme, nullptr, 1.0f), png);
}

} // namespace qlab::viz
