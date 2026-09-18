// Spec 21 §3.3 — phase disks (see PhaseDiskView.hpp).
#include "Viz/Views/PhaseDiskView.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Detail/ImGuiSupport.hpp"
#include "Viz/Math/Color.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Math/Phase.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz {
using namespace detail;

namespace {
constexpr float kPad = 6.0f;
constexpr float kLabelH = 14.0f;
} // namespace

ReductionRequest PhaseDiskView::wants(const ViewInput& in) const {
    ReductionRequest r;
    if (in.snapshot && in.snapshot->nQubits > math::kFullAmplitudeQubits)
        r.topAmplitudes = filter_.maxEntries > 0 ? filter_.maxEntries : math::kDefaultTopK;
    return r;
}

void PhaseDiskView::setFilter(const math::AmplitudeFilter& f) {
    filter_ = f;
    markDirty();
}

void PhaseDiskView::rebuild(const ViewInput& in) {
    selection_ = {};
    note_.clear();
    nQubits_ = in.qubitCount();
    const AmplitudeSource src = amplitudesFor(in, filter_);
    if (!src.selection) {
        note_ = src.note;
        return;
    }
    selection_ = *src.selection;
    nQubits_ = selection_.nQubits;
    if (filter_.order == math::AmplitudeOrder::ByMagnitude)
        std::stable_sort(selection_.entries.begin(), selection_.entries.end(),
                         [](const math::BasisEntry& a, const math::BasisEntry& b) { return a.probability > b.probability; });
}

void PhaseDiskView::layout() {
    const glm::vec2 body = bodySize();
    const std::size_t n = selection_.entries.size();
    if (n == 0 || body.x <= 0.0f || body.y <= 0.0f) {
        columns_ = rows_ = 1;
        cell_ = 0.0f;
        return;
    }
    // The column count that makes the square cells largest (the discs must stay circular).
    columns_ = 1;
    cell_ = 0.0f;
    for (std::size_t c = 1; c <= n; ++c) {
        const std::size_t r = (n + c - 1) / c;
        const float side = std::min(body.x / static_cast<float>(c), (body.y - kLabelH) / static_cast<float>(r));
        if (side > cell_) {
            cell_ = side;
            columns_ = c;
        }
    }
    rows_ = (n + columns_ - 1) / columns_;
    const float w = cell_ * static_cast<float>(columns_), h = cell_ * static_cast<float>(rows_);
    grid_ = {0.5 * (body.x - w), 0.5 * (body.y - h - kLabelH), 0.5 * (body.x - w) + w, 0.5 * (body.y - h - kLabelH) + h};
}

glm::vec2 PhaseDiskView::discCenter(std::size_t k) const {
    const std::size_t row = k / std::max<std::size_t>(1, columns_), col = k % std::max<std::size_t>(1, columns_);
    return {static_cast<float>(grid_.x0) + (static_cast<float>(col) + 0.5f) * cell_,
            static_cast<float>(grid_.y0) + (static_cast<float>(row) + 0.5f) * cell_ - 0.5f * kLabelH};
}

float PhaseDiskView::discRadius(std::size_t k) const {
    if (k >= selection_.entries.size()) return 0.0f;
    // Radius ∝ |a_i| with the largest amplitude of the shown set filling the cell.
    double maxAbs = 0.0;
    for (const math::BasisEntry& e : selection_.entries) maxAbs = std::max(maxAbs, std::sqrt(e.probability));
    const double a = std::sqrt(selection_.entries[k].probability);
    const float full = 0.5f * cell_ - kPad;
    return maxAbs > 0.0 ? full * static_cast<float>(a / maxAbs) : 0.0f;
}

std::optional<HitResult> PhaseDiskView::hitTest(glm::vec2 local) const {
    if (selection_.entries.empty() || cell_ <= 0.0f) return std::nullopt;
    for (std::size_t k = 0; k < selection_.entries.size(); ++k) {
        const glm::vec2 c = discCenter(k);
        const float r = std::max(discRadius(k), 0.3f * cell_); // the empty cell is still clickable
        if (glm::length(local - c) > r) continue;
        const math::BasisEntry& e = selection_.entries[k];
        HitResult h;
        h.kind = HitKind::BasisState;
        h.basisIndex = e.index;
        h.title = math::ketLabel(e.index, nQubits_, false, true);
        const data::FidelityClass cls = input().stateClass();
        h.readout.push_back({"a", math::formatPolar(e.amplitude), cls});
        h.readout.push_back({"", math::formatCartesian(e.amplitude), cls});
        h.readout.push_back({"|a|²", math::formatSig(e.probability), cls});
        h.readout.push_back({"arg a", math::formatAngle(e.phase), cls});
        return h;
    }
    return std::nullopt;
}

std::optional<std::string> PhaseDiskView::exportCsv() const {
    if (selection_.entries.empty()) return std::nullopt;
    std::string csv = "state,re,im,magnitude,phase\n";
    for (const math::BasisEntry& e : selection_.entries)
        csv += math::bitString(e.index, nQubits_) + "," + math::formatSig(e.amplitude.real(), 12) + "," +
               math::formatSig(e.amplitude.imag(), 12) + "," + math::formatSig(std::sqrt(e.probability), 12) + "," +
               math::formatSig(e.phase, 12) + "\n";
    return csv;
}

std::string PhaseDiskView::statusLine() const {
    std::string s = StateView::statusLine();
    if (selection_.entries.empty()) return s;
    if (!s.empty()) s += "  ";
    return s + std::to_string(selection_.entries.size()) + " of " + std::to_string(selection_.totalStates) + " states";
}

void PhaseDiskView::drawBody(DrawContext& ctx) {
    if (selection_.entries.empty()) return widgets::placeholder(ctx, note_.empty() ? "No basis state above the threshold" : note_);
    const VizTheme& theme = *ctx.theme;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto screen = [&](glm::vec2 p) { return ImVec2(origin.x + p.x, origin.y + p.y); };
    const bool small = cell_ < 34.0f;

    for (std::size_t k = 0; k < selection_.entries.size(); ++k) {
        const math::BasisEntry& e = selection_.entries[k];
        const glm::vec2 c = discCenter(k);
        const float r = discRadius(k);
        const glm::vec3 hue = math::phaseColor(e.phase);
        dl->AddCircle(screen(c), 0.5f * cell_ - kPad, toU32(theme.border), 0, 1.0f); // the unit-magnitude ring
        if (r > 0.5f) {
            dl->AddCircleFilled(screen(c), r, toU32(hue, 0.9f));
            // Radial line at arg a (screen y grows downward, so the angle is negated).
            const auto dx = static_cast<float>(std::cos(e.phase)), dy = static_cast<float>(-std::sin(e.phase));
            dl->AddLine(screen(c), screen({c.x + r * dx, c.y + r * dy}),
                        toU32(math::readableOn(hue, glm::vec3(1.0f), glm::vec3(0.0f))), 1.6f);
        }
        if (!small) {
            const std::string lbl = math::bitString(e.index, nQubits_);
            drawTextCentered(dl, screen({c.x, c.y + 0.5f * cell_ - 5.0f}), theme.textSecondary, lbl);
        }
    }
    // Spec 21 §4: a hue is never shown without its legend.
    widgets::phaseWheel(ctx, {origin.x + bodySize().x - 26.0f, origin.y + 26.0f}, 18.0f);
    ImGui::Dummy(ImVec2(bodySize().x, bodySize().y));
}

} // namespace qlab::viz
