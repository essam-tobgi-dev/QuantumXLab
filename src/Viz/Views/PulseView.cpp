// Spec 21 §3.14 — pulse viewer (see PulseView.hpp).
#include "Viz/Views/PulseView.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Detail/PlotSupport.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Math/Phase.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz {
using namespace detail;

namespace {
constexpr double kNsPerS = 1e9;
constexpr float kMinRowH = 54.0f;
} // namespace

data::FidelityClass PulseView::fidelity(const ViewInput&) const {
    // The envelope is the exact waveform the schedule holds, sampled on the device grid.
    return data::FidelityClass::Exact;
}

void PulseView::setWindow(double t0Ns, double t1Ns) {
    if (t0_ == t0Ns && t1_ == t1Ns)
        return;
    t0_ = t0Ns;
    t1_ = t1Ns;
    markDirty();
}

void PulseView::setPolar(bool on) {
    if (polar_ == on)
        return;
    polar_ = on;
    markDirty();
}

void PulseView::frameContent() {
    setWindow(0.0, 0.0);
}

void PulseView::rebuild(const ViewInput& in) {
    model_ = {};
    plots_.clear();
    note_.clear();
    hasPlayhead_ = in.hasPlayhead || in.playheadS > 0.0;
    playheadNs_ = in.playheadS * kNsPerS;
    if (!in.schedule || in.schedule->empty()) {
        note_ = "Schedule a program at pulse level to see its channels";
        return;
    }
    layout::PulseLayoutOptions o;
    o.t0Ns = t0_;
    o.t1Ns = t1_;
    o.qubits = qubitSubset();
    model_ = layout::buildPulseModel(*in.schedule, o);
    if (model_.rows.empty()) {
        note_ = qubitSubset().empty() ? "The schedule has no channels"
                                      : "No channel of the selected qubits";
        return;
    }
    plots_.resize(model_.rows.size());
    for (std::size_t k = 0; k < model_.rows.size(); ++k) {
        const layout::PulseTrace& t = model_.rows[k].trace;
        RowPlot& p = plots_[k];
        p.a.reserve(t.re.size());
        p.b.reserve(t.im.size());
        for (std::size_t i = 0; i < t.re.size(); ++i) {
            if (polar_) {
                p.a.push_back(std::hypot(t.re[i], t.im[i]));
                p.b.push_back(std::atan2(t.im[i], t.re[i]));
            } else {
                p.a.push_back(t.re[i]);
                p.b.push_back(t.im[i]);
            }
        }
        for (const layout::FrameMark& m : model_.rows[k].phaseJumps)
            p.phaseX.push_back(m.tNs);
    }
}

Rect PulseView::rowRect(std::size_t k) const {
    if (k >= model_.rows.size() || rowH_ <= 0.0f)
        return {};
    return {plot_.x0, plot_.y0 + static_cast<double>(k) * rowH_, plot_.x1,
            plot_.y0 + static_cast<double>(k + 1) * rowH_};
}

std::optional<HitResult> PulseView::hitTest(glm::vec2 local) const {
    if (model_.rows.empty() || plot_.width() <= 0.0 || rowH_ <= 0.0f)
        return std::nullopt;
    if (!plot_.contains(local.x, local.y))
        return std::nullopt;
    const auto k = static_cast<std::size_t>((local.y - plot_.y0) / rowH_);
    if (k >= model_.rows.size())
        return std::nullopt;
    const double tNs =
        model_.t0Ns + (model_.t1Ns - model_.t0Ns) * (local.x - plot_.x0) / plot_.width();
    const auto r = layout::readoutAt(model_, k, tNs);
    if (!r)
        return std::nullopt;
    const layout::PulseRow& row = model_.rows[k];
    HitResult h;
    h.kind = HitKind::Channel;
    h.row = static_cast<std::uint32_t>(k);
    h.qubit = QubitIndex{row.qubit};
    h.title = row.label + "  t = " + math::formatTime(tNs * 1e-9);
    constexpr data::FidelityClass kExact = data::FidelityClass::Exact;
    if (!row.acquire) {
        h.readout.push_back({"e(t)", math::formatCartesian(num::Complex(r->re, r->im)), kExact});
        h.readout.push_back({"|e|", math::formatSig(std::hypot(r->re, r->im)), kExact});
    }
    h.readout.push_back({"frame phase", math::formatAngle(r->framePhase), kExact});
    if (r->frequencyHz != 0.0)
        h.readout.push_back({"frequency", math::formatFrequency(r->frequencyHz), kExact});
    if (r->inAcquisition)
        h.readout.push_back({"acquisition", "window open", kExact});
    return h;
}

std::optional<std::string> PulseView::exportCsv() const {
    if (model_.rows.empty())
        return std::nullopt;
    std::string csv = "channel,t_ns,re,im\n";
    for (const layout::PulseRow& row : model_.rows)
        for (std::size_t k = 0; k < row.trace.tNs.size(); ++k)
            csv += row.label + "," + math::formatSig(row.trace.tNs[k], 12) + "," +
                   math::formatSig(row.trace.re[k], 12) + "," +
                   math::formatSig(row.trace.im[k], 12) + "\n";
    return csv;
}

std::string PulseView::statusLine() const {
    if (model_.rows.empty())
        return {};
    std::string s = std::to_string(model_.rows.size()) + " channels   " +
                    math::formatTime(model_.durationNs * 1e-9) +
                    "   dt = " + math::formatTime(model_.dtNs * 1e-9);
    for (const layout::PulseRow& row : model_.rows)
        if (row.trace
                .decimated) { // spec 22 §2: say so, the line is an envelope and not the samples
            s += "   decimated";
            break;
        }
    if (hasPlayhead_)
        s += "   playhead " + math::formatTime(playheadNs_ * 1e-9);
    return s;
}

void PulseView::drawBody(DrawContext& ctx) {
    if (model_.rows.empty())
        return widgets::placeholder(ctx, note_.empty() ? "No pulse channels" : note_);
    const VizTheme& theme = *ctx.theme;
    const PlotStyle style(theme);
    const glm::vec2 body = bodySize();
    const ImVec2 bodyOrigin = ImGui::GetCursorScreenPos();
    const int rows = static_cast<int>(model_.rows.size());
    rowH_ = std::max(kMinRowH, body.y / static_cast<float>(rows));
    plot_ = {0.0, 0.0, body.x, static_cast<double>(rowH_) * rows};

    // Stacked subplots sharing the time axis (spec 21 §3.14).
    if (!ImPlot::BeginSubplots("##pulses", rows, 1,
                               ImVec2(body.x, rowH_ * static_cast<float>(rows)),
                               ImPlotSubplotFlags_LinkAllX | ImPlotSubplotFlags_NoTitle))
        return;
    for (std::size_t k = 0; k < model_.rows.size(); ++k) {
        const layout::PulseRow& row = model_.rows[k];
        const RowPlot& p = plots_[k];
        if (!ImPlot::BeginPlot(row.label.c_str(), ImVec2(), ImPlotFlags_NoTitle))
            continue;
        const bool last = k + 1 == model_.rows.size();
        ImPlot::SetupAxes(last ? "t (ns)" : nullptr, row.label.c_str(),
                          last ? 0 : ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, model_.t0Ns, model_.t1Ns, ImPlotCond_Once);
        // Acquisition windows first, so the envelope draws over them.
        for (const layout::AcquireSpan& a : row.acquisitions) {
            const double xs[2] = {a.t0Ns, a.t1Ns};
            const double lo[2] = {-1.0, -1.0}, hi[2] = {1.0, 1.0};
            ImPlot::SetNextFillStyle(toImVec4(glm::vec3(theme.accentSoft), 0.18f));
            ImPlot::PlotShaded("acquire", xs, lo, hi, 2);
        }
        if (!p.a.empty()) {
            if (row.trace.decimated) { // min/max band under the mean line (spec 22 §2)
                ImPlot::SetNextFillStyle(toImVec4(glm::vec3(theme.accent), 0.20f));
                ImPlot::PlotShaded("##band", row.trace.tNs.data(), row.trace.reLo.data(),
                                   row.trace.reHi.data(), static_cast<int>(row.trace.tNs.size()));
            }
            ImPlot::SetNextLineStyle(toImVec4(glm::vec3(theme.accent), 1.0f), 1.6f);
            ImPlot::PlotLine(polar_ ? "|e|" : "Re", row.trace.tNs.data(), p.a.data(),
                             static_cast<int>(p.a.size()));
            ImPlot::SetNextLineStyle(toImVec4(glm::vec3(theme.accentSoft), 1.0f), 1.2f);
            ImPlot::PlotLine(polar_ ? "arg e" : "Im", row.trace.tNs.data(), p.b.data(),
                             static_cast<int>(p.b.size()));
        }
        if (!p.phaseX.empty()) { // frame phase jumps as vertical ticks
            ImPlot::SetNextLineStyle(toImVec4(glm::vec3(theme.warn), 0.8f), 1.0f);
            ImPlot::PlotInfLines("frame", p.phaseX.data(), static_cast<int>(p.phaseX.size()));
        }
        for (const layout::FrequencyMark& f : row.frequencyChanges)
            ImPlot::Annotation(f.tNs, 0.0, toImVec4(theme.textSecondary), ImVec2(4.0f, -4.0f), true,
                               "%s", math::formatFrequency(f.frequencyHz).c_str());
        if (hasPlayhead_)
            ImPlot::TagX(playheadNs_, toImVec4(theme.accent), "t");
        if (k == 0) {
            const ImVec2 p0 = ImPlot::GetPlotPos(), sz = ImPlot::GetPlotSize();
            plot_ = {p0.x - bodyOrigin.x, p0.y - bodyOrigin.y, p0.x - bodyOrigin.x + sz.x,
                     p0.y - bodyOrigin.y + sz.y * static_cast<double>(rows)};
            rowH_ = sz.y;
        }
        ImPlot::EndPlot();
    }
    ImPlot::EndSubplots();
}

} // namespace qlab::viz
