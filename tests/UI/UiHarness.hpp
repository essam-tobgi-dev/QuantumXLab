#pragma once
// Headless Dear ImGui + ImPlot for the UI tests: a real context with the SHIPPED fonts rasterised
// into the atlas and no renderer backend, so panel code runs exactly as in the application (layout,
// tables, docking, hit testing) and the draw lists are produced and discarded. No GL context is
// needed; tests SKIP when the pinned fonts are not on disk.
#include "UI/UI.hpp"
#include <imgui.h>
#include <implot.h>

namespace qlab::ui::test {

class UiHarness {
public:
    explicit UiHarness(float width = 1600.0f, float height = 1000.0f) : size_(width, height) {
        IMGUI_CHECKVERSION();
        ctx_ = ImGui::CreateContext();
        plot_ = ImPlot::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = size_;
        io.DeltaTime = 1.0f / 60.0f;
        Shell::configureImGui();
        ok_ = static_cast<bool>(resources_.applyScale(io.Fonts, 1.0f, 1.0f));
        if (ok_) {
            unsigned char* pixels = nullptr;
            int w = 0, h = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h); // nothing uploads it
        }
        (void)resources_.loadAssets("dark");
        resources_.bind(context_);
        context_.deltaS = 1.0 / 60.0;
    }
    ~UiHarness() {
        ImPlot::DestroyContext(plot_);
        ImGui::DestroyContext(ctx_);
    }
    UiHarness(const UiHarness&) = delete;
    UiHarness& operator=(const UiHarness&) = delete;

    // False when the pinned fonts are missing: the test SKIPs.
    bool ready() const { return ok_; }
    UiResources& resources() { return resources_; }
    UiContext& context() { return context_; }
    ImVec2 displaySize() const { return size_; }

    // One whole frame; `body` runs between NewFrame and Render.
    template <class F> void frame(F&& body, double dt = 1.0 / 60.0) {
        ImGui::GetIO().DeltaTime = static_cast<float>(dt);
        context_.timeS += dt;
        context_.deltaS = dt;
        ImGui::NewFrame();
        body();
        ImGui::Render();
        const ImDrawData* data = ImGui::GetDrawData();
        vertices_ = data != nullptr ? data->TotalVtxCount : 0;
    }
    // One frame drawing `shell` — the whole application UI.
    void frame(Shell& shell, double dt = 1.0 / 60.0) {
        frame([&] { shell.draw(context_); }, dt);
    }

    int vertices() const { return vertices_; }

private:
    ImVec2 size_;
    ImGuiContext* ctx_ = nullptr;
    ImPlotContext* plot_ = nullptr;
    UiResources resources_;
    UiContext context_;
    bool ok_ = false;
    int vertices_ = 0;
};

} // namespace qlab::ui::test
