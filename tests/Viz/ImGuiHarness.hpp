#pragma once
// Headless Dear ImGui + ImPlot for the view tests: a context with a built font atlas and no
// renderer backend. `draw()` code runs exactly as in the app (layout, hit testing, hover cards,
// clicks); the draw lists are produced and discarded. No GL context is needed.
#include "Viz/IStateView.hpp"
#include <functional>
#include <imgui.h>
#include <implot.h>

namespace qlab::viz::test {

class ImGuiHarness {
public:
    explicit ImGuiHarness(float width = 900.0f, float height = 600.0f) : size_(width, height) {
        IMGUI_CHECKVERSION();
        ctx_ = ImGui::CreateContext();
        plot_ = ImPlot::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = size_;
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h); // builds the atlas; nothing uploads it
    }
    ~ImGuiHarness() {
        ImPlot::DestroyContext(plot_);
        ImGui::DestroyContext(ctx_);
    }
    ImGuiHarness(const ImGuiHarness&) = delete;
    ImGuiHarness& operator=(const ImGuiHarness&) = delete;

    // Top-left of the window's content region in screen coordinates (constant across frames).
    ImVec2 windowPos() const { return ImVec2(10.0f, 10.0f); }
    ImVec2 windowSize() const { return ImVec2(size_.x - 20.0f, size_.y - 20.0f); }

    void mouseTo(ImVec2 screen) { ImGui::GetIO().AddMousePosEvent(screen.x, screen.y); }
    void mouseButton(bool down) { ImGui::GetIO().AddMouseButtonEvent(0, down); }

    // Screen position of the view's body when the header is hidden (ctx.showHeader = false): the
    // window's content origin. Body-local coordinates of a view add to this.
    ImVec2 bodyOrigin() const {
        const ImVec2 pad = ImGui::GetStyle().WindowPadding;
        return ImVec2(windowPos().x + pad.x, windowPos().y + pad.y);
    }

    // One UI frame with the view drawn inside a fixed window; advances the UI clock by `dt`.
    void frame(IStateView& view, DrawContext& ctx, double dt = 1.0 / 60.0) {
        ImGui::GetIO().DeltaTime = static_cast<float>(dt);
        ctx.timeS += dt;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(windowPos());
        ImGui::SetNextWindowSize(windowSize());
        ImGui::Begin("view", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        view.draw(ctx);
        drawCommands_ = ImGui::GetWindowDrawList()->CmdBuffer.Size;
        vertices_ = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        ImGui::End();
        ImGui::Render();
    }
    int vertices() const { return vertices_; }

private:
    ImVec2 size_;
    ImGuiContext* ctx_ = nullptr;
    ImPlotContext* plot_ = nullptr;
    int drawCommands_ = 0, vertices_ = 0;
};

} // namespace qlab::viz::test
