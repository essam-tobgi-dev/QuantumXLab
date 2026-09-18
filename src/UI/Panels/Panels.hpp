#pragma once
// Spec 19 §3 — factories of the panel catalog. One translation unit per panel (or per closely
// related pair); `Panel.cpp` assembles them in table order.
#include "UI/Panel.hpp"
#include <imgui.h>

namespace qlab::ui {

PanelPtr makeViewportPanel();
PanelPtr makeInspectorPanel();
PanelPtr makeComponentTreePanel();
PanelPtr makeCodeEditorPanel();
PanelPtr makeDiagnosticsPanel();
PanelPtr makeCircuitPanel();
PanelPtr makePulsePanel();
PanelPtr makeRunControlsPanel();
PanelPtr makeResultsPanel();
PanelPtr makeStateViewsPanel();
PanelPtr makePlotsPanel();
PanelPtr makeFitsPanel();
PanelPtr makeInstrumentsPanel();
PanelPtr makeFridgePanel();
PanelPtr makeEstimatesPanel();
PanelPtr makeTheoryPanel();
PanelPtr makeExamplesPanel();
PanelPtr makeProjectPanel();
PanelPtr makeLogPanel();
PanelPtr makeTourPanel();   // spec 17 §7.10 / 19 §3 "Guided tour"

// Spec 17 §7.10 — the narration card the Viewport draws over its picture while a tour plays
// (defined in TourPanel.cpp; draws nothing when `ctx.tour` is null or idle).
void drawTourOverlay(UiContext& ctx, ImVec2 imageMin, ImVec2 imageMax);

} // namespace qlab::ui
