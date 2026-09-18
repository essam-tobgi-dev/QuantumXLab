// Spec 21 §3.13 — circuit diagram: the ImGui body (canvas image, zoom, pan, minimap) and the PNG
// export. The scene itself is in CircuitViewDraw.cpp.
#include "Viz/Detail/ImGuiSupport.hpp"
#include "Viz/Views/CircuitView.hpp"
#include "Viz/Views/CircuitViewImpl.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz {
using namespace detail;

namespace {

constexpr float kWheelGain = 0.16f;   // zoom steps per wheel notch
constexpr float kMinimapWidthPx = 190.0f;

// Spec 21 §3.13: above 200 columns the diagram gets a minimap — the whole extent with the part the
// canvas currently shows drawn inside it.
void minimapOverlay(const DrawContext& ctx, ImVec2 origin, glm::vec2 body, const Rect& content, const Rect& visible) {
    if (content.width() <= 0.0 || content.height() <= 0.0) return;
    const float w = std::min(kMinimapWidthPx, 0.42f * body.x);
    const float h = std::max(10.0f, w * static_cast<float>(content.height() / content.width()));
    const ImVec2 a(origin.x + body.x - w - 10.0f, origin.y + 10.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), toU32(ctx.theme->bgPanel, 0.82f), ctx.theme->radiusSm);
    dl->AddRect(a, ImVec2(a.x + w, a.y + h), toU32(ctx.theme->border), ctx.theme->radiusSm);
    const auto mapX = [&](double x) { return a.x + static_cast<float>((x - content.x0) / content.width()) * w; };
    const auto mapY = [&](double y) { return a.y + static_cast<float>((y - content.y0) / content.height()) * h; };
    const ImVec2 v0(std::clamp(mapX(visible.x0), a.x, a.x + w), std::clamp(mapY(visible.y0), a.y, a.y + h));
    const ImVec2 v1(std::clamp(mapX(visible.x1), a.x, a.x + w), std::clamp(mapY(visible.y1), a.y, a.y + h));
    dl->AddRectFilled(v0, v1, toU32(ctx.theme->accent, 0.18f));
    dl->AddRect(v0, v1, toU32(ctx.theme->accent));
}

} // namespace

void CircuitView::aimCamera(glm::vec2 body) {
    // Orthographic, looking down −z: one world unit is one layout unit, so `toBody` on the CPU and
    // the GPU projection agree exactly (the hit test and the picture must not drift apart).
    gfx::Camera& cam = canvas_.camera();
    cam.setOrtho(true);
    cam.lookAt({0.0, 0.0, 10.0}, {0.0, 0.0, 0.0});
    cam.setClip(0.1, 40.0);
    cam.setAspect(body.y > 0.0f ? static_cast<double>(body.x) / static_cast<double>(body.y) : 1.0);
    gfx::Bookmark b = cam.bookmark();
    b.ortho = true;
    b.orthoHeight = zoom_ > 0.0 ? static_cast<double>(body.y) / zoom_ : 2.0;
    cam.set(b);
}

void CircuitView::drawBody(DrawContext& ctx) {
    if (layout_.rows.empty())
        return widgets::placeholder(ctx, note_.empty() ? "Compile a program to see its circuit" : note_);
    if (!ctx.gl) return widgets::placeholder(ctx, "The circuit diagram needs the GL canvas (no GL context)");
    const glm::vec2 body = bodySize();
    aimCamera(body);
    const float scale = std::max(1.0f, ctx.dpiScale);
    auto rendered = canvas_.render(*ctx.gl, static_cast<int>(body.x * scale), static_cast<int>(body.y * scale),
                                   scene(*ctx.theme, scale), ctx.timeS);
    if (!rendered || !canvas_.hasImage()) return widgets::placeholder(ctx, "GL canvas unavailable");

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(canvas_.textureId()), ImVec2(body.x, body.y), ImVec2(0, 1), ImVec2(1, 0));
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemHovered()) {
        if (io.MouseWheel != 0.0f) { // zoom about the cursor: the layout point under it stays put
            const glm::vec2 local{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
            const glm::dvec2 before = toLayout(local);
            setZoom(zoom_ * std::exp(static_cast<double>(kWheelGain * io.MouseWheel)));
            setPan(pan_ + (before - toLayout(local)));
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
            setPan(pan_ - glm::dvec2(io.MouseDelta.x, io.MouseDelta.y) / zoom_);
    }
    if (!note_.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 8.0f, origin.y + 6.0f));
        text(ctx.theme->textSecondary, note_);
    }
    if (minimap())
        minimapOverlay(ctx, origin, body, contentExtent(layout_),
                       {pan_.x, pan_.y, pan_.x + body.x / zoom_, pan_.y + body.y / zoom_});
}

Status CircuitView::renderPng(GlBackend& gl, const VizTheme& theme, int widthPx, int heightPx,
                              const std::filesystem::path& png) {
    setBodySize({static_cast<float>(widthPx), static_cast<float>(heightPx)});
    frameContent();
    aimCamera(bodySize());
    return canvas_.renderToPng(gl, widthPx, heightPx, scene(theme, 1.0f), png);
}

} // namespace qlab::viz
