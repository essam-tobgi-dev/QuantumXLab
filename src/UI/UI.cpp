// Spec 19 — the resource bundle the App builds once (see UI.hpp).
#include "UI/UI.hpp"
#include <algorithm>
#include <imgui.h>

namespace qlab::ui {

Status UiResources::loadAssets(std::string_view palette) {
    Status first{};
    const auto keep = [&](Status s) {
        if (!s && first) first = std::move(s);
    };
    if (auto t = Theme::load(palette)) theme = std::move(*t);
    else keep(std::unexpected(t.error()));           // the §1 fallback table stays in place
    if (auto s = Strings::load()) Strings::setGlobal(std::move(*s));
    else keep(std::unexpected(s.error()));           // every key then echoes itself
    if (auto a = TheoryAssets::load()) assets = std::move(*a);
    else keep(std::unexpected(a.error()));
    if (auto d = theory::TheoryIndex::load(theory::TheoryIndex::defaultDir())) theory = std::move(*d);
    else keep(std::unexpected(d.error()));
    registerLayoutSchema();
    return first;
}

Status UiResources::applyScale(ImFontAtlas* atlas, float dpiScale, float fontScale) {
    QXL_TRY_ASSIGN(FontSet built, FontSet::build(atlas, theme, dpiScale, fontScale));
    fonts = std::move(built);
    // Spec 20 §4/§7: the math layout cache is keyed on the font, so rebinding drops it.
    math.rebind(fonts);
    // The faces are rasterised at `dpiScale` times their logical size so that text is crisp on a
    // HiDPI display, but ImGui lays out in logical points (the GLFW backend keeps DisplaySize in
    // window units and puts the ratio in DisplayFramebufferScale). Without this the interface is
    // drawn `dpiScale` times too large — every control, not just the text.
    if (ImGui::GetCurrentContext() != nullptr)
        ImGui::GetIO().FontGlobalScale = 1.0f / std::max(0.25f, dpiScale);
    applyImGuiStyle(theme, fontScale);   // style metrics are logical points, not device pixels
    return {};
}

void UiResources::bind(UiContext& ctx, float dpiScale, float fontScale) const {
    ctx.theme = &theme;
    ctx.fonts = &fonts;
    ctx.math = const_cast<MathRenderers*>(&math);
    ctx.assets = &assets;
    ctx.theoryIndex = const_cast<theory::TheoryIndex*>(&theory);
    ctx.undo = const_cast<UndoStack*>(&undo);
    ctx.dpiScale = dpiScale;
    ctx.fontScale = fontScale;
}

} // namespace qlab::ui
