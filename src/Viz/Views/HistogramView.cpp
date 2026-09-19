// Spec 21 §3.7 — probability histogram (see HistogramView.hpp).
#include "Viz/Views/HistogramView.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Detail/PlotSupport.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz {
using namespace detail;

data::FidelityClass HistogramView::fidelity(const ViewInput& in) const {
    // Counts are Statistical; the overlay is Exact/Numerical. A view that mixes classes takes the
    // weakest one (spec 00 §5), and Statistical is weaker than both.
    return in.counts ? data::FidelityClass::Statistical : in.idealClass;
}

void HistogramView::setOrder(math::HistogramOrder order) {
    if (options_.order == order)
        return;
    options_.order = order;
    markDirty();
}
void HistogramView::setCumulative(bool on) {
    if (cumulative_ == on)
        return;
    cumulative_ = on;
    markDirty();
}
void HistogramView::setMarginalBits(std::vector<std::size_t> bits) {
    if (options_.marginalBits == bits)
        return;
    options_.marginalBits = std::move(bits);
    markDirty();
}

void HistogramView::rebuild(const ViewInput& in) {
    model_ = {};
    note_.clear();
    x_.clear();
    y_.clear();
    neg_.clear();
    pos_.clear();
    ideal_.clear();
    idealX_.clear();
    if (!in.counts) {
        note_ = "Run a program with shots to see the outcome distribution";
        return;
    }
    const std::span<const double> ideal = in.idealProbabilities
                                              ? std::span<const double>(*in.idealProbabilities)
                                              : std::span<const double>{};
    auto built = math::buildHistogram(*in.counts, ideal, options_);
    if (!built) {
        note_ = std::string(built.error().message);
        return;
    }
    model_ = std::move(*built);
    for (std::size_t k = 0; k < model_.bars.size(); ++k) {
        const math::HistogramBar& b = model_.bars[k];
        const double value = cumulative_ ? b.cumulative : b.estimate.pHat;
        x_.push_back(static_cast<double>(k));
        y_.push_back(value);
        // Wilson bounds as distances from p̂ (ImPlot takes ± lengths); the cumulative curve has
        // none.
        neg_.push_back(cumulative_ ? 0.0 : std::max(0.0, b.estimate.pHat - b.estimate.lo));
        pos_.push_back(cumulative_ ? 0.0 : std::max(0.0, b.estimate.hi - b.estimate.pHat));
        if (b.ideal && !cumulative_) {
            idealX_.push_back(static_cast<double>(k));
            ideal_.push_back(*b.ideal);
        }
    }
}

std::optional<HitResult> HistogramView::hitTest(glm::vec2 local) const {
    if (model_.bars.empty() || plot_.width() <= 0.0)
        return std::nullopt;
    if (!plot_.contains(local.x, local.y))
        return std::nullopt;
    const double n = static_cast<double>(model_.bars.size());
    const double t = (local.x - plot_.x0) / plot_.width();
    const auto k = static_cast<std::size_t>(std::floor(t * n));
    if (t < 0.0 || k >= model_.bars.size())
        return std::nullopt;
    const math::HistogramBar& b = model_.bars[k];
    HitResult h;
    h.kind = HitKind::BasisState;
    h.basisIndex = b.index;
    h.row = static_cast<std::uint32_t>(k);
    h.title = b.other ? "other" : "|" + b.label + ">";
    h.readout.push_back({"counts", std::to_string(b.count) + " / " + std::to_string(model_.shots),
                         data::FidelityClass::Statistical});
    h.readout.push_back({"p̂", math::formatSig(b.estimate.pHat), data::FidelityClass::Statistical});
    h.readout.push_back(
        {"68.3 % Wilson",
         "[" + math::formatSig(b.estimate.lo) + ", " + math::formatSig(b.estimate.hi) + "]",
         data::FidelityClass::Statistical});
    if (b.ideal)
        h.readout.push_back({"exact p", math::formatSig(*b.ideal), input().idealClass});
    return h;
}

std::optional<std::string> HistogramView::exportCsv() const {
    if (model_.bars.empty())
        return std::nullopt;
    std::string csv = "outcome,count,p_hat,wilson_lo,wilson_hi,ideal\n";
    for (const math::HistogramBar& b : model_.bars)
        csv += b.label + "," + std::to_string(b.count) + "," +
               math::formatSig(b.estimate.pHat, 12) + "," + math::formatSig(b.estimate.lo, 12) +
               "," + math::formatSig(b.estimate.hi, 12) + "," +
               (b.ideal ? math::formatSig(*b.ideal, 12) : std::string()) + "\n";
    return csv;
}

std::string HistogramView::statusLine() const {
    if (model_.bars.empty())
        return {};
    std::string s =
        std::to_string(model_.shots) + " shots  " + std::to_string(model_.distinct) + " outcomes";
    if (model_.hellinger)
        s += "  H = " + math::formatSig(*model_.hellinger);
    if (model_.totalVariation)
        s += "  TVD = " + math::formatSig(*model_.totalVariation);
    return s;
}

void HistogramView::drawBody(DrawContext& ctx) {
    if (model_.bars.empty())
        return widgets::placeholder(ctx, note_.empty() ? "No outcomes" : note_);
    const VizTheme& theme = *ctx.theme;
    const PlotStyle style(theme);
    const glm::vec2 body = bodySize();
    const ImVec2 bodyOrigin = ImGui::GetCursorScreenPos();
    if (ImPlot::BeginPlot("##histogram", ImVec2(body.x, body.y), ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("outcome  (bit 0 is rightmost)", cumulative_ ? "cumulative p̂" : "p̂",
                          kCategoryAxis, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, -0.75, static_cast<double>(model_.bars.size()) - 0.25,
                                ImPlotCond_Always);
        TickLabels ticks;
        const std::size_t stride = std::max<std::size_t>(1, model_.bars.size() / 24);
        for (std::size_t k = 0; k < model_.bars.size(); k += stride)
            ticks.add(static_cast<double>(k), model_.bars[k].label);
        ticks.apply(ImAxis_X1);

        ImPlot::SetNextFillStyle(toImVec4(glm::vec3(theme.accent), 0.85f));
        ImPlot::PlotBars("shots", x_.data(), y_.data(), static_cast<int>(x_.size()), 0.68);
        if (!cumulative_) {
            ImPlot::SetNextErrorBarStyle(toImVec4(theme.textPrimary), 3.0f, 1.4f);
            ImPlot::PlotErrorBars("shots", x_.data(), y_.data(), neg_.data(), pos_.data(),
                                  static_cast<int>(x_.size()));
        }
        if (!ideal_.empty()) {
            // Theory overlay: markers in the secondary ink (spec 22 §3), never a filled bar.
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Diamond, 4.0f, toImVec4(theme.theory()), 1.0f,
                                       toImVec4(theme.theory()));
            ImPlot::SetNextLineStyle(toImVec4(theme.theory()), 0.0f);
            ImPlot::PlotScatter("exact", idealX_.data(), ideal_.data(),
                                static_cast<int>(ideal_.size()));
        }
        const ImVec2 p0 = ImPlot::GetPlotPos(), sz = ImPlot::GetPlotSize();
        plot_ = {p0.x - bodyOrigin.x, p0.y - bodyOrigin.y, p0.x - bodyOrigin.x + sz.x,
                 p0.y - bodyOrigin.y + sz.y};
        ImPlot::EndPlot();
    }
}

} // namespace qlab::viz
