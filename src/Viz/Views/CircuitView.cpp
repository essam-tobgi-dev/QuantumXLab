// Spec 21 §3.13 — circuit diagram: stage selection, the cached moment/timed layout, the camera
// (zoom and pan in layout units), hit testing, the CSV export and the status line. The GL scene is
// in CircuitViewDraw.cpp and the SVG export in CircuitViewSvg.cpp.
#include "Viz/Views/CircuitView.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Views/CircuitViewImpl.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz {

namespace {
constexpr double kMarginPx = 14.0;   // body pixels kept free around the diagram when it is framed
constexpr double kWireHitBand = 0.3; // layout units around a wire that count as a click on it
} // namespace

data::FidelityClass CircuitView::fidelity(const ViewInput&) const {
    // The diagram is the program itself, so it is exact. The timed layout draws the gate durations
    // the scheduler took from the calibration, which are fitted measurements (spec 00 §5).
    return layout_.timed ? data::FidelityClass::Model : data::FidelityClass::Exact;
}

void CircuitView::setStage(CircuitStage s) {
    if (stage_ == s) return;
    stage_ = s;
    fitPending_ = true;
    markDirty();
}

void CircuitView::setTimed(bool on) {
    if (timed_ == on) return;
    timed_ = on;
    fitPending_ = true;
    markDirty();
}

void CircuitView::setZoom(double pixelsPerUnit) {
    const double z = std::clamp(pixelsPerUnit, kMinZoom, kMaxZoom);
    if (z == zoom_) return;
    zoom_ = z;
    fitPending_ = false;
    canvas_.invalidate();
}

void CircuitView::setPan(glm::dvec2 layoutUnits) {
    if (layoutUnits == pan_) return;
    pan_ = layoutUnits;
    fitPending_ = false;
    canvas_.invalidate();
}

// ------------------------------------------------------------------------------- model

void CircuitView::rebuild(const ViewInput& in) {
    layout_ = {};
    circuit_.reset();
    note_.clear();
    layoutMap_ = in.circuits.layout;
    hasPlayhead_ = in.hasPlayhead;
    playhead_ = in.playheadGate;

    // The requested stage when the compiler produced it, else the most compiled stage below it,
    // else the earliest one present (spec 19 §3: the toggle offers only the stages that exist).
    shown_ = stage_;
    circuit_ = in.circuits.at(stage_);
    for (std::size_t k = static_cast<std::size_t>(stage_); !circuit_ && k-- > 0;)
        if (in.circuits.stages[k]) {
            circuit_ = in.circuits.stages[k];
            shown_ = static_cast<CircuitStage>(k);
        }
    for (std::size_t k = static_cast<std::size_t>(stage_) + 1; !circuit_ && k < kCircuitStageCount; ++k)
        if (in.circuits.stages[k]) {
            circuit_ = in.circuits.stages[k];
            shown_ = static_cast<CircuitStage>(k);
        }
    if (!circuit_) {
        note_ = "Compile a program to see its circuit";
        return;
    }

    layout::CircuitLayoutOptions o;
    o.timed = timed_;
    o.layout = layoutMap_;
    // A SWAP of a routed circuit that no earlier stage contained was inserted by the router.
    if (shown_ == CircuitStage::Routed || shown_ == CircuitStage::Scheduled) {
        std::size_t before = 0;
        for (std::size_t k = 0; k < static_cast<std::size_t>(CircuitStage::Routed); ++k)
            if (in.circuits.stages[k]) before = std::max(before, layout::countSwaps(*in.circuits.stages[k]));
        o.markRoutingSwaps = before == 0;
    }
    auto laid = layout::layoutCircuit(*circuit_, o);
    if (!laid && o.timed) { // this stage carries no schedule record: show the moments instead
        o.timed = false;
        note_ = "No schedule on this stage: showing the moment layout";
        laid = layout::layoutCircuit(*circuit_, o);
    }
    if (!laid) {
        note_ = laid.error().message;
        circuit_.reset();
        return;
    }
    layout_ = std::move(*laid);
    fitPending_ = true;
}

void CircuitView::layout() {
    canvas_.invalidate();
    if (fitPending_) frameContent();
}

void CircuitView::frameContent() {
    const glm::vec2 body = bodySize();
    if (layout_.rows.empty() || body.x <= 0.0f || body.y <= 0.0f) {
        fitPending_ = true; // no body yet: fit on the first layout that has one
        return;
    }
    const Rect content = detail::contentExtent(layout_);
    const double w = std::max(1e-6, content.width()), h = std::max(1e-6, content.height());
    const double fit = std::min((static_cast<double>(body.x) - 2.0 * kMarginPx) / w,
                                (static_cast<double>(body.y) - 2.0 * kMarginPx) / h);
    zoom_ = std::clamp(fit, kMinZoom, kMaxZoom);
    // Centre the content: `pan_` is the layout point drawn at the body's top-left corner.
    pan_ = {content.x0 - 0.5 * (static_cast<double>(body.x) / zoom_ - w),
            content.y0 - 0.5 * (static_cast<double>(body.y) / zoom_ - h)};
    fitPending_ = false;
    canvas_.invalidate();
}

glm::vec2 CircuitView::toBody(glm::dvec2 p) const {
    return {static_cast<float>((p.x - pan_.x) * zoom_), static_cast<float>((p.y - pan_.y) * zoom_)};
}

glm::dvec2 CircuitView::toLayout(glm::vec2 p) const {
    return {pan_.x + static_cast<double>(p.x) / zoom_, pan_.y + static_cast<double>(p.y) / zoom_};
}

bool CircuitView::isCurrent(const layout::Glyph& g) const {
    return hasPlayhead_ && static_cast<std::uint64_t>(g.topoIndex) == playhead_;
}

float CircuitView::alphaOf(const layout::Glyph& g) const {
    if (!hasPlayhead_) return 1.0f;
    if (static_cast<std::uint64_t>(g.topoIndex) < playhead_) return 0.34f; // executed: dimmed
    return isCurrent(g) ? 1.0f : 0.88f;
}

// ------------------------------------------------------------------------------- interaction

std::optional<HitResult> CircuitView::hitTest(glm::vec2 local) const {
    if (layout_.rows.empty() || zoom_ <= 0.0) return std::nullopt;
    const glm::dvec2 p = toLayout(local);
    if (const layout::Glyph* g = layout_.glyphAt(p.x, p.y)) {
        HitResult h;
        h.kind = HitKind::Gate;
        h.gate = g->topoIndex;
        h.title = g->params.empty() ? g->label : g->label + "(" + g->params + ")";
        std::string wires;
        for (std::uint32_t r : g->controlRows) wires += (wires.empty() ? "" : ", ") + detail::wireLabelText(layout_.rows[r]);
        for (std::uint32_t r : g->targetRows)
            if (r < layout_.rows.size()) wires += (wires.empty() ? "" : ", ") + detail::wireLabelText(layout_.rows[r]);
        if (!wires.empty()) h.readout.push_back({g->controlRows.empty() ? "wires" : "control, target", wires,
                                                 data::FidelityClass::Exact});
        if (g->clbit) h.readout.push_back({"bit", g->params, data::FidelityClass::Exact});
        if (!layout_.timed) h.readout.push_back({"moment", std::to_string(g->moment), data::FidelityClass::Exact});
        if (g->startNs) {
            h.readout.push_back({"t start", math::formatWithUnit(*g->startNs, "ns"), data::FidelityClass::Model});
            h.readout.push_back({"duration", math::formatWithUnit(g->durationNs.value_or(0.0), "ns"),
                                 data::FidelityClass::Model});
        }
        if (g->routingSwap) h.readout.push_back({"routing", "SWAP inserted by the router", data::FidelityClass::Exact});
        if (hasPlayhead_)
            h.readout.push_back({"playhead", isCurrent(*g) ? "current gate"
                                             : static_cast<std::uint64_t>(g->topoIndex) < playhead_ ? "executed"
                                                                                                    : "pending",
                                 data::FidelityClass::Exact});
        return h;
    }
    // A click anywhere on a wire, label gutter included, selects that qubit (spec 21 §1.1).
    if (p.x < detail::kLabelGutter * -1.0 || p.x > layout_.width) return std::nullopt;
    const double rowF = std::floor(p.y);
    if (rowF < 0.0 || rowF >= static_cast<double>(layout_.rows.size())) return std::nullopt;
    const auto row = static_cast<std::uint32_t>(rowF);
    if (std::abs(p.y - layout_.rowY(row)) > kWireHitBand) return std::nullopt;
    const layout::WireLabel& w = layout_.rows[row];
    HitResult h;
    h.title = detail::wireLabelText(w);
    if (w.classical) {
        h.kind = HitKind::Row;
        h.row = row;
        h.readout.push_back({"classical register", std::to_string(w.bits) + " bits", data::FidelityClass::Exact});
        return h;
    }
    h.kind = HitKind::Qubit;
    // The shared selection holds PHYSICAL qubits: a virtual wire is mapped through the layout.
    h.qubit = QubitIndex{w.physical ? row : (row < layoutMap_.size() ? layoutMap_[row] : row)};
    if (w.physical && w.virtualQubit)
        h.readout.push_back({"program qubit", "q" + std::to_string(*w.virtualQubit), data::FidelityClass::Exact});
    else if (!w.physical && row < layoutMap_.size())
        h.readout.push_back({"physical qubit", "$" + std::to_string(layoutMap_[row]), data::FidelityClass::Exact});
    return h;
}

// ------------------------------------------------------------------------------- text output

std::optional<std::string> CircuitView::exportCsv() const {
    if (layout_.glyphs.empty()) return std::nullopt;
    std::string csv = "gate,kind,label,params,targets,controls,moment,column,depth,start_ns,duration_ns\n";
    const auto rows = [](const std::vector<std::uint32_t>& v) {
        std::string s;
        for (std::uint32_t r : v) s += (s.empty() ? "" : " ") + std::to_string(r);
        return s;
    };
    for (const layout::Glyph& g : layout_.glyphs) {
        csv += std::to_string(g.topoIndex) + "," + std::to_string(static_cast<int>(g.kind)) + "," + g.label + ",\"" +
               g.params + "\"," + rows(g.targetRows) + "," + rows(g.controlRows) + "," + std::to_string(g.moment) + "," +
               std::to_string(g.column) + "," + std::to_string(g.depth) + "," +
               (g.startNs ? math::formatSig(*g.startNs, 12) : std::string()) + "," +
               (g.durationNs ? math::formatSig(*g.durationNs, 12) : std::string()) + "\n";
    }
    return csv;
}

std::string CircuitView::statusLine() const {
    if (layout_.rows.empty()) return {};
    std::string s(circuitStageName(shown_));
    s += "   " + std::to_string(layout_.qubitRows) + " qubits   " + std::to_string(layout_.glyphs.size()) + " ops";
    if (layout_.timed) s += "   " + math::formatWithUnit(layout_.durationNs, "ns");
    else s += "   " + std::to_string(layout_.moments) + " moments   " + std::to_string(layout_.columns) + " columns";
    if (layout_.routingSwaps > 0) s += "   " + std::to_string(layout_.routingSwaps) + " routing SWAPs";
    if (hasPlayhead_) s += "   gate " + std::to_string(playhead_);
    return s;
}

} // namespace qlab::viz
