// Spec 19 §2/§3 — the per-frame snapshot record (see Context.hpp).
#include "UI/Context.hpp"

namespace qlab::ui {

std::string_view SessionView::statusKey() const {
    switch (status) {
    case Status::Idle:
        return "run.status_idle";
    case Status::Compiling:
        return "run.status_compiling";
    case Status::Running:
        return "run.status_running";
    case Status::Done:
        return "run.status_done";
    case Status::Failed:
        return "diagnostics.errors";
    }
    return "run.status_idle";
}

viz::DrawContext UiContext::drawContext(const viz::VizTheme& vizTheme) const {
    viz::DrawContext dc;
    dc.theme = &vizTheme;
    dc.selection = selection;
    dc.gl = gl;
    dc.dpiScale = dpiScale;
    dc.timeS = timeS;
    dc.asciiKets = asciiKets;
    dc.reducedMotion = reducedMotion;
    dc.showHeader = true;
    dc.openTheory = cmd.openTheory;
    dc.requestExport = cmd.requestExport;
    return dc;
}

} // namespace qlab::ui
