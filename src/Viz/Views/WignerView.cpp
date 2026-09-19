// Spec 21 §3.17 — Wigner heatmap (see WignerView.hpp).
#include "Viz/Views/WignerView.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Detail/PlotSupport.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz {
using namespace detail;

namespace {
std::size_t workOf(const num::Matrix& rho, const math::WignerOptions& o) {
    return rho.rows * rho.rows * o.nx * o.ny;
}
} // namespace

void WignerView::setMode(std::shared_ptr<const num::Matrix> rho, std::string label) {
    mode_ = std::move(rho);
    modeLabel_ = std::move(label);
    markDirty();
}

void WignerView::setAxes(const math::WignerOptions& axes) {
    options_ = axes;
    markDirty();
}

ReductionRequest WignerView::wants(const ViewInput&) const {
    ReductionRequest r;
    if (mode_ && workOf(*mode_, options_) > kInlineWork) {
        r.wigner = options_;
        r.modeState = mode_;
    }
    return r;
}

data::FidelityClass WignerView::fidelity(const ViewInput& in) const {
    // W is a quadrature of the truncated ρ evaluated on a finite grid: Numerical at best, and never
    // better than the state it was computed from (spec 00 §5).
    const data::FidelityClass state = in.stateClass();
    return state == data::FidelityClass::Exact ? data::FidelityClass::Numerical : state;
}

void WignerView::rebuild(const ViewInput& in) {
    grid_.reset();
    photons_.clear();
    heat_.clear();
    note_.clear();
    if (in.reductions && in.reductions->wigner) {
        grid_ = in.reductions->wigner;
    } else if (mode_ && !mode_->empty()) {
        if (workOf(*mode_, options_) > kInlineWork) {
            note_ = "Waiting for the Wigner grid from the run";
            setStale(true);
        } else if (auto g = math::wignerGrid(*mode_, options_)) {
            grid_ = std::move(*g);
        } else {
            note_ = std::string(g.error().message);
        }
    } else {
        note_ = "Bind an oscillator mode (resonator, cavity or motional mode) to see its Wigner "
                "function";
    }
    if (mode_)
        photons_ = math::photonNumberDistribution(*mode_);
    if (!grid_)
        return;
    // ImPlot lays a heatmap out row 0 at the TOP of `bounds`, while the grid stores p_min first.
    const math::WignerGrid& g = *grid_;
    heat_.resize(g.axes.nx * g.axes.ny);
    for (std::size_t iy = 0; iy < g.axes.ny; ++iy)
        for (std::size_t ix = 0; ix < g.axes.nx; ++ix)
            heat_[(g.axes.ny - 1 - iy) * g.axes.nx + ix] = g.at(ix, iy);
}

std::optional<HitResult> WignerView::hitTest(glm::vec2 local) const {
    if (!grid_ || plot_.width() <= 0.0 || plot_.height() <= 0.0)
        return std::nullopt;
    if (!plot_.contains(local.x, local.y))
        return std::nullopt;
    const math::WignerGrid& g = *grid_;
    const double u = (local.x - plot_.x0) / plot_.width(),
                 v = (local.y - plot_.y0) / plot_.height();
    const auto ix = std::min<std::size_t>(
        g.axes.nx - 1, static_cast<std::size_t>(u * static_cast<double>(g.axes.nx)));
    const auto iyTop = std::min<std::size_t>(
        g.axes.ny - 1, static_cast<std::size_t>(v * static_cast<double>(g.axes.ny)));
    const std::size_t iy = g.axes.ny - 1 - iyTop; // screen y grows downward, p grows upward
    HitResult h;
    h.kind = HitKind::MatrixElement;
    h.element = std::pair<std::uint32_t, std::uint32_t>{static_cast<std::uint32_t>(ix),
                                                        static_cast<std::uint32_t>(iy)};
    const data::FidelityClass cls = fidelity(input());
    h.title = "W(x = " + math::formatSig(g.x(ix)) + ", p = " + math::formatSig(g.p(iy)) + ")";
    h.readout.push_back({"W", math::formatSig(g.at(ix, iy)), cls});
    h.readout.push_back(
        {"α",
         math::formatCartesian(num::Complex((g.x(ix)) / std::sqrt(2.0), g.p(iy) / std::sqrt(2.0))),
         cls});
    return h;
}

std::optional<std::string> WignerView::exportCsv() const {
    if (!grid_)
        return std::nullopt;
    const math::WignerGrid& g = *grid_;
    std::string csv = "x,p,W\n";
    for (std::size_t iy = 0; iy < g.axes.ny; ++iy)
        for (std::size_t ix = 0; ix < g.axes.nx; ++ix)
            csv += math::formatSig(g.x(ix), 8) + "," + math::formatSig(g.p(iy), 8) + "," +
                   math::formatSig(g.at(ix, iy), 12) + "\n";
    return csv;
}

std::string WignerView::statusLine() const {
    if (!grid_)
        return {};
    std::string s = modeLabel_.empty() ? std::string() : modeLabel_ + "   ";
    if (mode_)
        s += "N_max = " + std::to_string(mode_->rows > 0 ? mode_->rows - 1 : 0) + "   ";
    // Spec 21 §3.17: the negativity volume is the headline number of this view.
    return s + "negativity = " + math::formatSig(grid_->negativity) +
           "   ∫W = " + math::formatSig(grid_->integral);
}

void WignerView::drawBody(DrawContext& ctx) {
    if (!grid_ || heat_.empty())
        return widgets::placeholder(ctx, note_.empty() ? "No Wigner grid" : note_);
    const VizTheme& theme = *ctx.theme;
    const PlotStyle style(theme);
    const ColormapScope cmap(Colormap::Diverging);
    const math::WignerGrid& g = *grid_;
    const double limit = g.symmetricLimit();
    const glm::vec2 body = bodySize();
    const ImVec2 bodyOrigin = ImGui::GetCursorScreenPos();
    const float barW = 64.0f;
    const float photonW =
        body.x > 420.0f ? 0.3f * body.x : 0.0f; // P(n) beside the map when there is room
    const float mapW = std::max(80.0f, body.x - barW - photonW);

    if (ImPlot::BeginPlot("##wigner", ImVec2(mapW, body.y),
                          ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("x", "p", ImPlotAxisFlags_NoGridLines, ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxisLimits(ImAxis_X1, g.axes.xMin, g.axes.xMax, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, g.axes.pMin, g.axes.pMax, ImPlotCond_Always);
        ImPlot::PlotHeatmap("W", heat_.data(), static_cast<int>(g.axes.ny),
                            static_cast<int>(g.axes.nx), -limit, limit, nullptr,
                            ImPlotPoint(g.axes.xMin, g.axes.pMin),
                            ImPlotPoint(g.axes.xMax, g.axes.pMax));
        const ImVec2 p0 = ImPlot::GetPlotPos(), sz = ImPlot::GetPlotSize();
        plot_ = {p0.x - bodyOrigin.x, p0.y - bodyOrigin.y, p0.x - bodyOrigin.x + sz.x,
                 p0.y - bodyOrigin.y + sz.y};
        ImPlot::EndPlot();
    }
    // Spec 22 §4 / 21 §4: the diverging scale is always shown with its range, W = 0 at the neutral
    // colour.
    ImGui::SameLine();
    ImPlot::ColormapScale("W", -limit, limit, ImVec2(barW - 10.0f, body.y), "%.3f");
    if (photonW > 0.0f && !photons_.empty()) {
        ImGui::SameLine();
        if (ImPlot::BeginPlot("##photons", ImVec2(photonW - 8.0f, body.y),
                              ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
            ImPlot::SetupAxes("n", "P(n)", kCategoryAxis, ImPlotAxisFlags_AutoFit);
            ImPlot::SetNextFillStyle(toImVec4(glm::vec3(theme.accent), 0.9f));
            ImPlot::PlotBars("P(n)", photons_.data(), static_cast<int>(photons_.size()), 0.7);
            ImPlot::EndPlot();
        }
    }
}

} // namespace qlab::viz
