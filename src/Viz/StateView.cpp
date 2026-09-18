// Spec 21 §1, §4 — behaviour shared by every view (see StateView.hpp).
#include "Viz/StateView.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Detail/ImGuiSupport.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>

namespace qlab::viz {
using namespace detail;

namespace {
constexpr double kHoverDelayS = 0.15;   // spec 21 §4
constexpr float kClickSlopPx = 4.0f;    // a drag that moved further orbits the camera, it does not select
} // namespace

std::string hitKey(const HitResult& h) {
    std::string k = std::to_string(static_cast<int>(h.kind));
    if (h.qubit) k += "q" + std::to_string(h.qubit->get());
    if (h.edge) k += "e" + std::to_string(h.edge->first.get()) + "-" + std::to_string(h.edge->second.get());
    if (h.basisIndex) k += "b" + std::to_string(*h.basisIndex);
    if (h.element) k += "m" + std::to_string(h.element->first) + "," + std::to_string(h.element->second);
    if (h.gate) k += "g" + std::to_string(*h.gate);
    if (h.row) k += "r" + std::to_string(*h.row);
    return k;
}

StateView::Stamp StateView::stampOf(const ViewInput& in) {
    Stamp s;
    std::size_t k = 0;
    s.pointers[k++] = in.snapshot.get();
    s.pointers[k++] = in.reductions.get();
    s.pointers[k++] = in.counts.get();
    s.pointers[k++] = in.idealProbabilities.get();
    s.pointers[k++] = in.measurement.get();
    for (const auto& c : in.circuits.stages) s.pointers[k++] = c.get();
    s.pointers[k++] = in.schedule.get();
    s.pointers[k++] = in.device.get();
    s.pointers[k++] = in.calibration.get();
    s.pointers[k++] = in.lindblad.get();
    s.pointers[k++] = in.trajectories.get();
    s.pointers[k++] = in.code.get();
    s.pointers[k++] = in.lattice.get();
    s.playheadGate = in.playheadGate;
    s.playheadS = in.playheadS;
    s.hasPlayhead = in.hasPlayhead;
    return s;
}

void StateView::update(const ViewInput& in) {
    const Stamp stamp = stampOf(in);
    if (haveInput_ && !dirty_ && stamp == stamp_) return; // nothing changed: O(1)
    input_ = in;
    stamp_ = stamp;
    haveInput_ = true;
    dirty_ = false;
    stale_ = !wants(in).empty() && in.reductionsStale();
    rebuild(in);
    ++rebuilds_;
    layoutDirty_ = true;
    if (bodySize_.x > 0.0f && bodySize_.y > 0.0f) {
        layout();
        layoutDirty_ = false;
    }
}

void StateView::markDirty() {
    dirty_ = true;
    if (haveInput_) update(ViewInput(input_));
}

void StateView::setBodySize(glm::vec2 size) {
    size = glm::max(size, glm::vec2(0.0f));
    if (size == bodySize_ && !layoutDirty_) return;
    bodySize_ = size;
    if (size.x > 0.0f && size.y > 0.0f) {
        layout();
        layoutDirty_ = false;
    }
}

void StateView::setQubitSubset(std::span<const QubitIndex> qubits) {
    std::vector<QubitIndex> next;
    for (QubitIndex q : qubits)
        if (std::find(next.begin(), next.end(), q) == next.end()) next.push_back(q);
    if (next == subset_) return;
    subset_ = std::move(next);
    markDirty();
}

std::vector<QubitIndex> StateView::shownQubits(std::uint32_t n) const {
    std::vector<QubitIndex> out;
    if (subset_.empty()) {
        for (std::uint32_t q = 0; q < n; ++q) out.emplace_back(q);
        return out;
    }
    for (QubitIndex q : subset_)
        if (q.get() < n) out.push_back(q);
    return out;
}

std::string StateView::statusLine() const {
    if (!input_.snapshot) return {};
    std::string s = "gate " + std::to_string(input_.snapshot->gateIndex);
    if (input_.snapshot->simTimePs > 0.0) s += "  t = " + math::formatTime(input_.snapshot->simTimePs * 1e-12);
    return s;
}

std::optional<HitResult> StateView::click(glm::vec2 local, SelectionModel* selection, bool additive) {
    auto hit = hitTest(local);
    if (!hit || !*hit) return std::nullopt;
    if (selection) {
        switch (hit->kind) {
        case HitKind::Qubit:
            if (hit->qubit) selection->selectQubit(*hit->qubit, additive);
            break;
        case HitKind::Edge:
            if (hit->edge) selection->selectEdge(hit->edge->first, hit->edge->second);
            break;
        case HitKind::BasisState: // a second click on the focused state releases the filter (spec 21 §3.4)
            if (hit->basisIndex)
                selection->setBasisFocus(selection->basisFocus() == hit->basisIndex ? std::nullopt : hit->basisIndex);
            break;
        default: break;
        }
    }
    onClicked(*hit, selection);
    if (onClick_) onClick_(*hit);
    return hit;
}

void StateView::draw(DrawContext& ctx) {
    if (!ctx.theme) return;
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 availAll = ImGui::GetContentRegionAvail();
    const float headerH = widgets::headerHeight(ctx);
    const Rect body{cursor.x, cursor.y + headerH, cursor.x + availAll.x, cursor.y + availAll.y};
    widgets::header(ctx, *this, haveInput_ ? fidelity(input_) : data::FidelityClass::Exact, body);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    setBodySize({avail.x, avail.y});
    if (ctx.selection && ctx.selection->revision() != selectionRevision_) {
        selectionRevision_ = ctx.selection->revision();
        onSelectionChanged(*ctx.selection);
    }
    drawBody(ctx);

    // ---- hover readout, click → selection, keyboard (spec 21 §4)
    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 local{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
    const bool inside = local.x >= 0.0f && local.y >= 0.0f && local.x <= avail.x && local.y <= avail.y &&
                        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows);
    if (!inside) {
        hoverKey_.clear();
        return;
    }
    const auto hit = hitTest(local);
    if (hit && *hit) {
        const std::string key = hitKey(*hit);
        if (key != hoverKey_) {
            hoverKey_ = key;
            hoverSince_ = ctx.timeS;
            if (onHover_) onHover_(*hit);
        }
        if (ctx.selection) ctx.selection->setHoveredQubit(hit->qubit);
        if (ctx.timeS - hoverSince_ >= kHoverDelayS) widgets::readoutCard(ctx, *hit);
    } else {
        hoverKey_.clear();
        if (ctx.selection) ctx.selection->setHoveredQubit(std::nullopt);
    }
    const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    if (released && io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] <= kClickSlopPx * kClickSlopPx)
        click(local, ctx.selection, io.KeyShift);
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) frameContent();
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) resetCamera();
        if (ctx.selection)
            for (int d = 0; d < 9; ++d) // keys 1–9 select qubits 0–8
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + d), false) &&
                    static_cast<std::uint32_t>(d) < input_.qubitCount())
                    ctx.selection->selectQubit(QubitIndex{static_cast<std::uint32_t>(d)}, io.KeyShift);
    }
}

} // namespace qlab::viz
