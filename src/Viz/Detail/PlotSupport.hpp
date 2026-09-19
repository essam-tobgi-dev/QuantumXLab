#pragma once
// INTERNAL to src/Viz: the ImPlot backend of spec 21 §1.2 (histograms, amplitude bars, Schmidt
// spectrum, pulse viewer, populations, trajectories, Wigner heatmap). One place for the theme
// styling, the colormaps built from `viz::math` LUTs, and the hover/cursor plumbing so that every
// plot view reads the same. Included by .cpp files only; ImPlot never appears in a public header.
#include "Viz/Detail/ImGuiSupport.hpp"
#include "Viz/Math/Color.hpp"
#include "Viz/Math/Phase.hpp"
#include <implot.h>
#include <string>
#include <vector>

namespace qlab::viz::detail {

inline ImVec4 toImVec4(const glm::vec3& c, float a = 1.0f) {
    return ImVec4(c.r, c.g, c.b, a);
}

// Styles the current ImPlot context from the theme for the lifetime of the object (spec 19 §1:
// no view holds a colour of its own).
class PlotStyle {
  public:
    explicit PlotStyle(const VizTheme& theme) {
        ImPlot::PushStyleColor(ImPlotCol_FrameBg, toImVec4(theme.bgPanel));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, toImVec4(theme.bgPanel));
        ImPlot::PushStyleColor(ImPlotCol_PlotBorder, toImVec4(theme.border));
        ImPlot::PushStyleColor(ImPlotCol_AxisText, toImVec4(theme.textSecondary));
        ImPlot::PushStyleColor(ImPlotCol_AxisGrid,
                               toImVec4(glm::vec4(glm::vec3(theme.border), 0.5f)));
        ImPlot::PushStyleColor(ImPlotCol_LegendBg, toImVec4(theme.bgRaised));
        ImPlot::PushStyleColor(ImPlotCol_LegendText, toImVec4(theme.textPrimary));
        ImPlot::PushStyleColor(ImPlotCol_InlayText, toImVec4(theme.textSecondary));
        ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(6.0f, 4.0f));
    }
    ~PlotStyle() {
        ImPlot::PopStyleVar();
        ImPlot::PopStyleColor(8);
    }
    PlotStyle(const PlotStyle&) = delete;
    PlotStyle& operator=(const PlotStyle&) = delete;
};

// The three colormaps the plot views need, added to the ImPlot context once and looked up by name
// afterwards: the cyclic phase wheel of spec 21 §2.4, the sequential scale and the diverging scale
// of spec 22 §4 (W = 0 at the neutral colour). Sampling `math::` keeps the plots, the GL views and
// the legends on one LUT.
enum class Colormap { Phase, Sequential, Diverging };

inline ImPlotColormap colormap(Colormap which) {
    const char* name = which == Colormap::Phase        ? "qxlPhase"
                       : which == Colormap::Sequential ? "qxlSequential"
                                                       : "qxlDiverging";
    const ImPlotColormap existing = ImPlot::GetColormapIndex(name);
    if (existing != -1)
        return existing;
    constexpr int kEntries = 64;
    std::vector<ImVec4> cols;
    cols.reserve(kEntries);
    for (int k = 0; k < kEntries; ++k) {
        const double t = static_cast<double>(k) / (kEntries - 1);
        cols.push_back(toImVec4(
            which == Colormap::Phase ? math::phaseColorAtHue(static_cast<double>(k) / kEntries)
            : which == Colormap::Sequential ? math::sequentialColor(t)
                                            : math::divergingColor(2.0 * t - 1.0)));
    }
    return ImPlot::AddColormap(name, cols.data(), kEntries, false);
}

// RAII colormap push.
class ColormapScope {
  public:
    explicit ColormapScope(Colormap which) { ImPlot::PushColormap(colormap(which)); }
    ~ColormapScope() { ImPlot::PopColormap(); }
    ColormapScope(const ColormapScope&) = delete;
    ColormapScope& operator=(const ColormapScope&) = delete;
};

// Axis flags shared by the categorical axes (bar charts): no grid, ticks supplied by the view.
inline constexpr ImPlotAxisFlags kCategoryAxis =
    ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoHighlight;

// Tick label storage for SetupAxisTicks, which takes `const char* const*`.
class TickLabels {
  public:
    void add(double value, std::string label) {
        values_.push_back(value);
        labels_.push_back(std::move(label));
    }
    void apply(ImAxis axis, bool keepDefault = false) {
        pointers_.clear();
        pointers_.reserve(labels_.size());
        for (const std::string& s : labels_)
            pointers_.push_back(s.c_str());
        if (!values_.empty())
            ImPlot::SetupAxisTicks(axis, values_.data(), static_cast<int>(values_.size()),
                                   pointers_.data(), keepDefault);
    }
    bool empty() const { return values_.empty(); }
    std::size_t size() const { return values_.size(); }

  private:
    std::vector<double> values_;
    std::vector<std::string> labels_;
    std::vector<const char*> pointers_;
};

} // namespace qlab::viz::detail
