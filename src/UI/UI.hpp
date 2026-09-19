#pragma once
#include <filesystem>
#include <memory>
// Umbrella header for qlab::ui (spec 19, spec 20 §5–§7) — the design system, the panel catalog and
// the shell the App drives.
//
//   Theme / Metrics     the spec 19 §1 tokens from Assets/Lang/theme.json, with `applyImGuiStyle`
//                       replacing every ImGui default and `imguiStyleMismatches` proving it (§9);
//                       `Theme::readableText` keeps every label at ≥ 4.5:1 contrast (§8).
//   FontSet             the pinned faces: Inter for UI text, JetBrains Mono for code and readouts,
//                       rasterised at the current DPI with FreeType hinting off (§1, §7).
//   Strings             every visible label, from Assets/Lang/strings.en.json (`tr("panels.log")`).
//   format::            unit-aware display and parsing of every numeric (§5.1, §5.2).
//   UndoStack           the 200-entry command stack, grouped by drag gesture (§5.3).
//   Shortcuts           the §5 keyboard table, remappable and JSON round-tripping.
//   UiContext           the per-frame snapshot record every panel reads, plus the commands it
//   posts. Panel / makeAllPanels   the §3 catalog: 19 panels, each with its workspace and its state
//   slice. Shell               the top bar, the dockspace, the workspace presets and the
//   Physical-lab
//                       toggle; `draw()` once per frame between NewFrame and Render.
//   LayoutState         §4 persistence: one ImGui ini per workspace plus each panel's JSON.
//   BasicMathRenderer + ImGuiMathFont/ImGuiMathCanvas + EquationView
//                       the LaTeX engine of spec 20 wired to an ImDrawList, with hover → term
//                       definition and live value, click → theory anchor.
//   theory::TheoryIndex the Markdown corpus behind the Theory Browser (spec 20 §5–§6).
//   editor::            the headless editor model (highlighting, markers, completion, brackets,
//                       go-to-definition, undo) and the `CodeEditor` widget over it.
//
// ImGui and ImPlot appear only inside `src/UI`, `src/Viz` and `src/App` (spec 19 lint).
#include "Graphics/GlObjects.hpp"
#include "UI/Context.hpp"
#include "UI/Editor/CodeEditor.hpp"
#include "UI/Editor/Document.hpp"
#include "UI/Editor/EditorModel.hpp"
#include "UI/Fonts.hpp"
#include "UI/Format.hpp"
#include "UI/Layout.hpp"
#include "UI/Math/BasicMathRenderer.hpp"
#include "UI/Math/MathLayout.hpp"
#include "UI/Panel.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Shell.hpp"
#include "UI/Shortcuts.hpp"
#include "UI/Strings.hpp"
#include "UI/Theme.hpp"
#include "UI/Theory/TheoryIndex.hpp"
#include "UI/UndoStack.hpp"
#include "UI/Widgets/EquationView.hpp"
#include "UI/Widgets/Equations.hpp"
#include "UI/Widgets/MathImGui.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"

namespace qlab::ui {

// Everything the UI owns, built once by the App: the theme and its fonts, the string table, the
// theory corpora, the math renderer and the undo stack. `bind(ctx)` fills the presentation half of
// a `UiContext`; the App fills the model half.
struct UiResources {
    Theme theme = Theme::fallback(true);
    FontSet fonts;
    MathRenderers math;
    TheoryAssets assets;
    theory::TheoryIndex theory;
    UndoStack undo;
    // The product mark at the left of the menu bar (Assets/Icons/logo.png). Loaded by the App
    // once a GL context exists; absent in headless tests, and the bar then shows the name only.
    std::unique_ptr<gfx::Texture2D> logo;
    Status loadLogo(const std::filesystem::path& png = {});

    // Loads theme.json, strings.en.json, the theory corpora and the docs; reports the first failure
    // but always leaves usable fallbacks in place.
    Status loadAssets(std::string_view palette = "dark");
    // Rebuilds the font atlas and the math cache for a new DPI or user font scale (spec 19 §7,
    // 20 §4) and re-applies the ImGui style.
    Status applyScale(ImFontAtlas* atlas, float dpiScale, float fontScale = 1.0f);
    // Fills the presentation fields of `ctx` (theme, fonts, math, assets, undo, scales).
    void bind(UiContext& ctx, float dpiScale = 1.0f, float fontScale = 1.0f) const;
};

} // namespace qlab::ui
