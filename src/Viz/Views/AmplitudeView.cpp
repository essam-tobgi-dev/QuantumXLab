// Spec 21 §3.2 — amplitude bars (see AmplitudeView.hpp).
#include "Viz/Views/AmplitudeView.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Detail/PlotSupport.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz {
using namespace detail;

ReductionRequest AmplitudeView::wants(const ViewInput& in) const {
    ReductionRequest r;
    // Above 20 qubits the O(2^n) scan leaves the UI thread (spec 21 §2.3): the run sends the top-k.
    if (in.snapshot && in.snapshot->nQubits > math::kFullAmplitudeQubits)
        r.topAmplitudes = filter_.maxEntries > 0 ? filter_.maxEntries : math::kDefaultTopK;
    return r;
}

void AmplitudeView::setShowProbability(bool on) {
    if (probability_ == on) return;
    probability_ = on;
    markDirty();
}
void AmplitudeView::setDecimalLabels(bool on) {
    if (decimal_ == on) return;
    decimal_ = on;
    markDirty();
}
void AmplitudeView::setFilter(const math::AmplitudeFilter& f) {
    filter_ = f;
    markDirty();
}

std::string AmplitudeView::label(std::uint64_t index) const {
    return math::ketLabel(index, nQubits_, decimal_, true);
}

void AmplitudeView::rebuild(const ViewInput& in) {
    selection_ = {};
    heights_.clear();
    positions_.clear();
    note_.clear();
    marginal_ = fromRun_ = false;
    nQubits_ = in.qubitCount();

    const AmplitudeSource src = amplitudesFor(in, filter_);
    if (!src.selection) {
        note_ = src.note;
        return;
    }
    fromRun_ = src.fromRun;
    selection_ = *src.selection;
    nQubits_ = selection_.nQubits;

    const std::vector<QubitIndex> subset = qubitSubset().empty() ? std::vector<QubitIndex>{}
                                                                : shownQubits(selection_.nQubits);
    if (!subset.empty() && subset.size() < selection_.nQubits) {
        // Spec 21 §3.2: a qubit subset shows the MARGINAL distribution — probabilities, no phase.
        std::vector<double> full(std::size_t{1} << selection_.nQubits, 0.0);
        for (const math::BasisEntry& e : selection_.entries)
            if (e.index < full.size()) full[e.index] = e.probability;
        if (auto marg = math::marginalProbabilities(full, selection_.nQubits, subset)) {
            math::AmplitudeSelection sel;
            sel.nQubits = static_cast<std::uint32_t>(subset.size());
            sel.totalStates = marg->size();
            for (std::size_t k = 0; k < marg->size(); ++k) {
                if (filter_.threshold > 0.0 && (*marg)[k] <= filter_.threshold) continue;
                sel.entries.push_back({k, num::Complex{std::sqrt((*marg)[k]), 0.0}, (*marg)[k], 0.0});
                sel.shownProbability += (*marg)[k];
            }
            sel.aboveThreshold = sel.entries.size();
            sel.totalProbability = 1.0;
            selection_ = std::move(sel);
            nQubits_ = selection_.nQubits;
            marginal_ = true;
            note_ = "marginal over " + subsetCaption(subset);
        }
    }
    if (filter_.order == math::AmplitudeOrder::ByMagnitude)
        std::stable_sort(selection_.entries.begin(), selection_.entries.end(),
                         [](const math::BasisEntry& a, const math::BasisEntry& b) { return a.probability > b.probability; });

    heights_.reserve(selection_.entries.size());
    for (std::size_t k = 0; k < selection_.entries.size(); ++k) {
        const math::BasisEntry& e = selection_.entries[k];
        heights_.push_back(probability_ || marginal_ ? e.probability : std::sqrt(e.probability));
        positions_.push_back(static_cast<double>(k));
    }
}

std::optional<HitResult> AmplitudeView::hitTest(glm::vec2 local) const {
    if (selection_.entries.empty() || plot_.width() <= 0.0) return std::nullopt;
    if (!plot_.contains(local.x, local.y)) return std::nullopt;
    const double n = static_cast<double>(selection_.entries.size());
    const double t = (local.x - plot_.x0) / plot_.width();
    const auto k = static_cast<std::size_t>(std::floor(t * n));
    if (t < 0.0 || k >= selection_.entries.size()) return std::nullopt;
    const math::BasisEntry& e = selection_.entries[k];
    HitResult h;
    h.kind = HitKind::BasisState;
    h.basisIndex = e.index;
    h.title = label(e.index);
    const data::FidelityClass cls = input().stateClass();
    if (marginal_) {
        h.readout.push_back({"p", math::formatSig(e.probability), cls});
        h.readout.push_back({"marginal over", subsetCaption(shownQubits(input().qubitCount())), cls});
        return h;
    }
    h.readout.push_back({"a", math::formatPolar(e.amplitude), cls});
    h.readout.push_back({"", math::formatCartesian(e.amplitude), cls});
    h.readout.push_back({"|a|²", math::formatSig(e.probability), cls});
    h.readout.push_back({"arg a", math::formatAngle(e.phase), cls});
    return h;
}

std::optional<std::string> AmplitudeView::exportCsv() const {
    if (selection_.entries.empty()) return std::nullopt;
    std::string csv = marginal_ ? "state,probability\n" : "state,re,im,probability,phase\n";
    for (const math::BasisEntry& e : selection_.entries) {
        csv += math::bitString(e.index, nQubits_) + ",";
        if (!marginal_)
            csv += math::formatSig(e.amplitude.real(), 12) + "," + math::formatSig(e.amplitude.imag(), 12) + ",";
        csv += math::formatSig(e.probability, 12);
        if (!marginal_) csv += "," + math::formatSig(e.phase, 12);
        csv += "\n";
    }
    return csv;
}

std::string AmplitudeView::statusLine() const {
    std::string s = StateView::statusLine();
    if (selection_.entries.empty()) return s;
    if (!s.empty()) s += "  ";
    s += std::to_string(selection_.entries.size()) + " of " + std::to_string(selection_.totalStates) + " states";
    if (selection_.truncated || fromRun_)
        s += "  mass " + math::formatSig(selection_.shownProbability, 4); // spec 21 §3.2: top-k mass shown
    return s;
}

void AmplitudeView::drawBody(DrawContext& ctx) {
    if (selection_.entries.empty()) return widgets::placeholder(ctx, note_.empty() ? "No basis state above the threshold" : note_);
    const VizTheme& theme = *ctx.theme;
    const PlotStyle style(theme);
    const glm::vec2 body = bodySize();
    const ImVec2 bodyOrigin = ImGui::GetCursorScreenPos(); // body-local (0,0): what hitTest works in
    const float wheel = marginal_ ? 0.0f : 54.0f; // room for the phase legend (spec 21 §2.4)
    if (ImPlot::BeginPlot("##amplitudes", ImVec2(body.x - wheel, body.y), ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        const char* yLabel = marginal_ ? "p" : probability_ ? "|a|²" : "|a|";
        ImPlot::SetupAxes("basis state  (bit 0 is rightmost)", yLabel, kCategoryAxis, ImPlotAxisFlags_AutoFit);
        const double n = static_cast<double>(heights_.size());
        ImPlot::SetupAxisLimits(ImAxis_X1, -0.75, n - 0.25, ImPlotCond_Always);
        TickLabels ticks;
        const std::size_t stride = std::max<std::size_t>(1, heights_.size() / 24);
        for (std::size_t k = 0; k < selection_.entries.size(); k += stride)
            ticks.add(static_cast<double>(k), label(selection_.entries[k].index));
        ticks.apply(ImAxis_X1);

        // One PlotBars per bar so that each carries its own phase colour (spec 21 §3.2). The bar
        // count is bounded by the top-k of §2.3, so this stays O(min(2^n, k)).
        for (std::size_t k = 0; k < heights_.size(); ++k) {
            const glm::vec3 fill = marginal_ ? glm::vec3(theme.neutral()) : math::phaseColor(selection_.entries[k].phase);
            ImPlot::SetNextFillStyle(toImVec4(fill, 0.95f));
            const double x = positions_[k], y = heights_[k];
            ImPlot::PlotBars("##bar", &x, &y, 1, 0.72);
        }
        const ImVec2 p0 = ImPlot::GetPlotPos(), sz = ImPlot::GetPlotSize();
        plot_ = {p0.x - bodyOrigin.x, p0.y - bodyOrigin.y, p0.x - bodyOrigin.x + sz.x, p0.y - bodyOrigin.y + sz.y};
        ImPlot::EndPlot();
    }
    if (!marginal_) {
        ImGui::SameLine();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        widgets::phaseWheel(ctx, {p.x + 24.0f, p.y + 0.5f * body.y}, 20.0f);
        ImGui::Dummy(ImVec2(wheel - 6.0f, body.y));
    }
}

} // namespace qlab::viz
