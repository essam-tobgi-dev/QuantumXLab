// Spec 21 §3.15–3.16 — populations vs time and the trajectory view (see TimeSeriesViews.hpp).
#include "Viz/Views/TimeSeriesViews.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Detail/PlotSupport.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz {
using namespace detail;

namespace {
constexpr double kNsPerS = 1e9;
const char* kLevelName[3] = {"P0", "P1", "P2 (leakage)"};

// Nearest sample index to a data x, for the hover readout.
std::size_t nearest(std::span<const double> xs, double x) {
    if (xs.empty()) return 0;
    const auto it = std::lower_bound(xs.begin(), xs.end(), x);
    std::size_t k = static_cast<std::size_t>(it - xs.begin());
    if (k >= xs.size()) k = xs.size() - 1;
    if (k > 0 && x - xs[k - 1] < xs[k] - x) --k;
    return k;
}
} // namespace

// ------------------------------------------------------------------ populations (spec 21 §3.15)

data::FidelityClass PopulationsView::fidelity(const ViewInput& in) const {
    return in.lindblad ? in.lindblad->cls : data::FidelityClass::Numerical;
}

void PopulationsView::setSite(std::uint32_t site) {
    if (site_ == site) return;
    site_ = site;
    markDirty();
}

std::span<const double> PopulationsView::level(std::size_t j) const {
    return j < levels_.size() ? std::span<const double>(levels_[j]) : std::span<const double>{};
}

void PopulationsView::rebuild(const ViewInput& in) {
    t_.clear();
    levels_.clear();
    purity_.clear();
    blochNorm_.clear();
    driveT_.clear();
    driveA_.clear();
    note_.clear();
    hasPlayhead_ = in.hasPlayhead || in.playheadS > 0.0;
    playheadNs_ = in.playheadS * kNsPerS;
    if (!in.lindblad || in.lindblad->samples.empty()) {
        note_ = "Run a pulse-level (Lindblad) simulation to see the populations";
        return;
    }
    const LindbladSeries& s = *in.lindblad;
    // The subset selects the site when one is set; otherwise the explicitly chosen site.
    std::uint32_t site = site_;
    if (!qubitSubset().empty()) site = qubitSubset().front().get();
    if (site >= std::max<std::uint32_t>(1, s.sites)) site = 0;
    site_ = site;
    const std::uint32_t levels = std::max<std::uint32_t>(2, s.levels);
    levels_.assign(levels, {});
    for (const qsim::TimeSample& sample : s.samples) {
        const std::size_t base = static_cast<std::size_t>(site) * levels;
        if (base + levels > sample.populations.size()) continue;
        t_.push_back(sample.timeS * kNsPerS);
        for (std::uint32_t j = 0; j < levels; ++j) levels_[j].push_back(sample.populations[base + j]);
    }
    for (std::size_t k = 0; k < s.purity.size() && k < t_.size(); ++k) purity_.push_back(s.purity[k]);
    if (site < s.blochNorm.size())
        for (std::size_t k = 0; k < s.blochNorm[site].size() && k < t_.size(); ++k) blochNorm_.push_back(s.blochNorm[site][k]);
    // The drive envelope of this site's channel, drawn faintly underneath (spec 21 §3.15).
    if (in.schedule) {
        layout::PulseLayoutOptions o;
        o.maxPoints = 800;
        const QubitIndex only[1] = {QubitIndex{site}};
        o.qubits = only;
        const layout::PulseModel model = layout::buildPulseModel(*in.schedule, o);
        if (const layout::PulseRow* row = model.row(pulse::ChannelId::drive(site))) {
            driveT_ = row->trace.tNs;
            for (std::size_t k = 0; k < row->trace.re.size(); ++k)
                driveA_.push_back(std::hypot(row->trace.re[k], row->trace.im[k]));
        }
    }
    if (t_.empty()) note_ = "The Lindblad series carries no samples for this site";
}

std::optional<HitResult> PopulationsView::hitTest(glm::vec2 local) const {
    if (t_.empty() || plot_.width() <= 0.0 || !plot_.contains(local.x, local.y)) return std::nullopt;
    const double t = t_.front() + (t_.back() - t_.front()) * (local.x - plot_.x0) / plot_.width();
    const std::size_t k = nearest(t_, t);
    HitResult h;
    h.kind = HitKind::Sample;
    h.qubit = QubitIndex{site_};
    h.row = static_cast<std::uint32_t>(k);
    const data::FidelityClass cls = fidelity(input());
    h.title = "q" + std::to_string(site_) + "  t = " + math::formatTime(t_[k] / kNsPerS);
    for (std::size_t j = 0; j < levels_.size(); ++j)
        h.readout.push_back({j < 3 ? kLevelName[j] : ("P" + std::to_string(j)), math::formatSig(levels_[j][k]), cls});
    if (k < purity_.size()) h.readout.push_back({"Tr ρ²", math::formatSig(purity_[k]), cls});
    if (k < blochNorm_.size()) h.readout.push_back({"|r|", math::formatSig(blochNorm_[k]), cls});
    return h;
}

std::optional<std::string> PopulationsView::exportCsv() const {
    if (t_.empty()) return std::nullopt;
    std::string csv = "t_ns";
    for (std::size_t j = 0; j < levels_.size(); ++j) csv += ",P" + std::to_string(j);
    csv += ",purity,bloch_norm\n";
    for (std::size_t k = 0; k < t_.size(); ++k) {
        csv += math::formatSig(t_[k], 12);
        for (const auto& lv : levels_) csv += "," + math::formatSig(lv[k], 12);
        csv += "," + (k < purity_.size() ? math::formatSig(purity_[k], 12) : std::string());
        csv += "," + (k < blochNorm_.size() ? math::formatSig(blochNorm_[k], 12) : std::string()) + "\n";
    }
    return csv;
}

std::string PopulationsView::statusLine() const {
    if (t_.empty()) return {};
    std::string s = "q" + std::to_string(site_) + "   " + std::to_string(t_.size()) + " samples over " +
                    math::formatTime(t_.back() / kNsPerS);
    if (levels_.size() > 2 && !levels_[2].empty())
        s += "   max leakage " + math::formatSig(*std::max_element(levels_[2].begin(), levels_[2].end()));
    return s;
}

void PopulationsView::drawBody(DrawContext& ctx) {
    if (t_.empty()) return widgets::placeholder(ctx, note_.empty() ? "No populations" : note_);
    const VizTheme& theme = *ctx.theme;
    const PlotStyle style(theme);
    const glm::vec2 body = bodySize();
    const ImVec2 bodyOrigin = ImGui::GetCursorScreenPos();
    if (ImPlot::BeginPlot("##populations", ImVec2(body.x, body.y), ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("t (ns)", "population");
        ImPlot::SetupAxisLimits(ImAxis_Y1, -0.02, 1.02, ImPlotCond_Always);
        ImPlot::SetupAxis(ImAxis_Y2, "|r|, Tr ρ²", ImPlotAxisFlags_AuxDefault);
        ImPlot::SetupAxisLimits(ImAxis_Y2, -0.02, 1.02, ImPlotCond_Always);
        if (!driveA_.empty()) { // the drive envelope, faint, under the curves
            ImPlot::SetNextFillStyle(toImVec4(glm::vec3(theme.textSecondary), 0.12f));
            ImPlot::PlotShaded("drive", driveT_.data(), driveA_.data(), static_cast<int>(driveA_.size()), 0.0);
        }
        for (std::size_t j = 0; j < levels_.size(); ++j) {
            ImPlot::SetNextLineStyle(toImVec4(glm::vec3(theme.qubitColor(static_cast<std::uint32_t>(j))), 1.0f), 1.8f);
            ImPlot::PlotLine(j < 3 ? kLevelName[j] : "P", t_.data(), levels_[j].data(), static_cast<int>(t_.size()));
        }
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
        if (!purity_.empty()) {
            ImPlot::SetNextLineStyle(toImVec4(glm::vec3(theme.textSecondary), 0.9f), 1.2f);
            ImPlot::PlotLine("Tr ρ²", t_.data(), purity_.data(), static_cast<int>(purity_.size()));
        }
        if (!blochNorm_.empty()) {
            ImPlot::SetNextLineStyle(toImVec4(glm::vec3(theme.accentSoft), 0.9f), 1.2f);
            ImPlot::PlotLine("|r|", t_.data(), blochNorm_.data(), static_cast<int>(blochNorm_.size()));
        }
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
        if (hasPlayhead_) ImPlot::TagX(playheadNs_, toImVec4(theme.accent), "t");
        const ImVec2 p0 = ImPlot::GetPlotPos(), sz = ImPlot::GetPlotSize();
        plot_ = {p0.x - bodyOrigin.x, p0.y - bodyOrigin.y, p0.x - bodyOrigin.x + sz.x, p0.y - bodyOrigin.y + sz.y};
        ImPlot::EndPlot();
    }
}

// ------------------------------------------------------------------ trajectories (spec 21 §3.16)

void TrajectoryView::rebuild(const ViewInput& in) {
    traces_.clear();
    t_.clear();
    meanZ_.clear();
    lo_.clear();
    hi_.clear();
    note_.clear();
    drawn_ = 0;
    trajectories_ = 0;
    if (!in.trajectories) {
        note_ = "Run a Monte-Carlo trajectory simulation to see the ensemble";
        return;
    }
    const TrajectoryEnsemble& e = *in.trajectories;
    qubit_ = e.qubit;
    trajectories_ = e.trajectories;
    for (std::size_t k = 0; k < e.timeS.size() && k < e.meanZ.size(); ++k) {
        t_.push_back(e.timeS[k] * kNsPerS);
        meanZ_.push_back(e.meanZ[k]);
        const double s = k < e.stderrZ.size() ? e.stderrZ[k] : 0.0;
        lo_.push_back(e.meanZ[k] - s);
        hi_.push_back(e.meanZ[k] + s);
    }
    drawn_ = std::min(kMaxDrawn, e.traces.size());
    for (std::size_t k = 0; k < drawn_; ++k) {
        const TrajectoryTrace& src = e.traces[k];
        Trace t;
        for (std::size_t i = 0; i < src.timeS.size() && i < src.z.size(); ++i) {
            t.t.push_back(src.timeS[i] * kNsPerS);
            t.z.push_back(src.z[i]);
        }
        for (double jump : src.jumpTimesS) { // a marker at each quantum jump, on that trajectory
            t.jumpT.push_back(jump * kNsPerS);
            t.jumpZ.push_back(t.t.empty() ? 0.0 : t.z[nearest(t.t, jump * kNsPerS)]);
        }
        traces_.push_back(std::move(t));
    }
    if (t_.empty() && traces_.empty()) note_ = "The ensemble carries no samples";
}

std::optional<HitResult> TrajectoryView::hitTest(glm::vec2 local) const {
    if (t_.empty() || plot_.width() <= 0.0 || !plot_.contains(local.x, local.y)) return std::nullopt;
    const double t = t_.front() + (t_.back() - t_.front()) * (local.x - plot_.x0) / plot_.width();
    const std::size_t k = nearest(t_, t);
    HitResult h;
    h.kind = HitKind::Sample;
    h.qubit = QubitIndex{qubit_};
    h.row = static_cast<std::uint32_t>(k);
    h.title = "q" + std::to_string(qubit_) + "  t = " + math::formatTime(t_[k] / kNsPerS);
    h.readout.push_back({"⟨Z⟩", math::formatSig(meanZ_[k]) + " ± " + math::formatSig(hi_[k] - meanZ_[k], 2),
                         data::FidelityClass::Statistical});
    h.readout.push_back({"trajectories", std::to_string(trajectories_), data::FidelityClass::Statistical});
    return h;
}

std::optional<std::string> TrajectoryView::exportCsv() const {
    if (t_.empty()) return std::nullopt;
    std::string csv = "t_ns,mean_z,stderr\n";
    for (std::size_t k = 0; k < t_.size(); ++k)
        csv += math::formatSig(t_[k], 12) + "," + math::formatSig(meanZ_[k], 12) + "," +
               math::formatSig(hi_[k] - meanZ_[k], 12) + "\n";
    return csv;
}

std::string TrajectoryView::statusLine() const {
    if (t_.empty() && traces_.empty()) return {};
    std::string s = "q" + std::to_string(qubit_) + "   " + std::to_string(trajectories_) + " trajectories";
    if (traces_.size() > drawn_ || drawn_ == kMaxDrawn)
        s += "   " + std::to_string(drawn_) + " drawn"; // the rest contribute to the mean only
    return s;
}

void TrajectoryView::drawBody(DrawContext& ctx) {
    if (t_.empty() && traces_.empty()) return widgets::placeholder(ctx, note_.empty() ? "No trajectories" : note_);
    const VizTheme& theme = *ctx.theme;
    const PlotStyle style(theme);
    const glm::vec2 body = bodySize();
    const ImVec2 bodyOrigin = ImGui::GetCursorScreenPos();
    if (ImPlot::BeginPlot("##trajectories", ImVec2(body.x, body.y), ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("t (ns)", "⟨Z⟩");
        ImPlot::SetupAxisLimits(ImAxis_Y1, -1.05, 1.05, ImPlotCond_Always);
        ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.0f);
        for (const Trace& tr : traces_) {
            if (tr.t.size() < 2) continue;
            ImPlot::SetNextLineStyle(toImVec4(glm::vec3(theme.textSecondary), 0.14f), 1.0f);
            ImPlot::PlotLine("##traj", tr.t.data(), tr.z.data(), static_cast<int>(tr.t.size()));
            if (!tr.jumpT.empty()) {
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross, 3.0f, toImVec4(glm::vec3(theme.warn), 0.5f), 1.0f,
                                           toImVec4(glm::vec3(theme.warn), 0.5f));
                ImPlot::PlotScatter("##jumps", tr.jumpT.data(), tr.jumpZ.data(), static_cast<int>(tr.jumpT.size()));
            }
        }
        ImPlot::PopStyleVar();
        if (!meanZ_.empty()) {
            ImPlot::SetNextFillStyle(toImVec4(glm::vec3(theme.accent), 0.22f));
            ImPlot::PlotShaded("± σ/√N", t_.data(), lo_.data(), hi_.data(), static_cast<int>(t_.size()));
            ImPlot::SetNextLineStyle(toImVec4(glm::vec3(theme.accent), 1.0f), 2.2f);
            ImPlot::PlotLine("mean ⟨Z⟩", t_.data(), meanZ_.data(), static_cast<int>(t_.size()));
        }
        const ImVec2 p0 = ImPlot::GetPlotPos(), sz = ImPlot::GetPlotSize();
        plot_ = {p0.x - bodyOrigin.x, p0.y - bodyOrigin.y, p0.x - bodyOrigin.x + sz.x, p0.y - bodyOrigin.y + sz.y};
        ImPlot::EndPlot();
    }
}

} // namespace qlab::viz
