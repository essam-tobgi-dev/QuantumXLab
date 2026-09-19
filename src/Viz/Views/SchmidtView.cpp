// Spec 21 §3.9 — Schmidt spectrum (see SchmidtView.hpp).
#include "Viz/Views/SchmidtView.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Detail/PlotSupport.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Views/StateSource.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz {
using namespace detail;

namespace {
constexpr std::uint32_t kMaxPartition = 14; // spec 21 §3.9: |A| ≤ 14
// Above this many qubits the O(2^n) SVD belongs to the run, not the UI thread (spec 21 §2.3).
constexpr std::uint32_t kInlineQubits = 14;
} // namespace

std::vector<QubitIndex> SchmidtView::defaultPartition(std::uint32_t n) const {
    std::vector<QubitIndex> a;
    for (std::uint32_t q = 0; q < std::min(n / 2 == 0 ? 1u : n / 2, kMaxPartition); ++q)
        a.emplace_back(q);
    return a;
}

ReductionRequest SchmidtView::wants(const ViewInput& in) const {
    ReductionRequest r;
    const std::uint32_t n = in.qubitCount();
    // n == 0 is "no register yet": asking the run for the spectrum of a cut through a register that
    // does not exist names an out-of-range qubit, which `computeReductions` rejects as an error.
    if (n > kInlineQubits) {
        std::vector<QubitIndex> a =
            qubitSubset().empty()
                ? defaultPartition(n)
                : std::vector<QubitIndex>(qubitSubset().begin(), qubitSubset().end());
        if (!a.empty() && a.size() <= kMaxPartition)
            r.schmidtPartition = std::move(a);
    }
    return r;
}

void SchmidtView::setLogScale(bool on) {
    if (log_ == on)
        return;
    log_ = on;
    markDirty();
}

void SchmidtView::rebuild(const ViewInput& in) {
    spectrum_.reset();
    note_.clear();
    x_.clear();
    y_.clear();
    const std::uint32_t n = in.qubitCount();
    partition_ = qubitSubset().empty() ? defaultPartition(n) : shownQubits(n);
    if (partition_.size() > kMaxPartition) {
        note_ = "Schmidt spectrum needs a bipartition of at most 14 qubits (spec 21 §3.9)";
        return;
    }
    if (partition_.empty() || partition_.size() >= n) {
        note_ = n == 0 ? "Run a program to see the Schmidt spectrum" : "Pick a bipartition A | Ā";
        return;
    }
    // The run's spectrum wins when it matches the requested cut; otherwise compute it here for the
    // registers small enough for the UI thread.
    if (in.reductions && in.reductions->schmidt &&
        in.reductions->schmidt->partition == partition_) {
        spectrum_ = in.reductions->schmidt;
    } else if (in.snapshot && in.snapshot->amplitudes && n <= kInlineQubits) {
        auto s = math::schmidtSpectrum(*in.snapshot->amplitudes, n, partition_);
        if (s)
            spectrum_ = std::move(*s);
        else
            note_ = std::string(s.error().message);
    } else if (in.snapshot) {
        note_ = in.snapshot->amplitudes
                    ? "Waiting for the Schmidt spectrum from the run"
                    : "The Schmidt decomposition needs a pure state (state-vector snapshot)";
        setStale(true);
    } else {
        note_ = "Run a program to see the Schmidt spectrum";
    }
    if (!spectrum_)
        return;
    for (std::size_t k = 0; k < spectrum_->coefficients.size(); ++k) {
        x_.push_back(static_cast<double>(k));
        y_.push_back(spectrum_->coefficients[k]);
    }
}

std::optional<HitResult> SchmidtView::hitTest(glm::vec2 local) const {
    if (!spectrum_ || x_.empty() || plot_.width() <= 0.0)
        return std::nullopt;
    if (!plot_.contains(local.x, local.y))
        return std::nullopt;
    const double t = (local.x - plot_.x0) / plot_.width();
    const auto k = static_cast<std::size_t>(std::floor(t * static_cast<double>(x_.size())));
    if (t < 0.0 || k >= x_.size())
        return std::nullopt;
    const double lambda = spectrum_->coefficients[k];
    HitResult h;
    h.kind = HitKind::Row;
    h.row = static_cast<std::uint32_t>(k);
    h.title = "λ" + std::to_string(k);
    const data::FidelityClass cls = input().stateClass();
    h.readout.push_back({"λ", math::formatSig(lambda), cls});
    h.readout.push_back({"λ²", math::formatSig(lambda * lambda), cls});
    return h;
}

std::optional<std::string> SchmidtView::exportCsv() const {
    if (!spectrum_)
        return std::nullopt;
    std::string csv = "k,lambda,lambda_squared\n";
    for (std::size_t k = 0; k < spectrum_->coefficients.size(); ++k) {
        const double l = spectrum_->coefficients[k];
        csv += std::to_string(k) + "," + math::formatSig(l, 12) + "," + math::formatSig(l * l, 12) +
               "\n";
    }
    return csv;
}

std::string SchmidtView::statusLine() const {
    if (!spectrum_)
        return StateView::statusLine();
    return "A = " + subsetCaption(partition_) + "   rank " + std::to_string(spectrum_->rank) +
           "   S_A = " + math::formatSig(spectrum_->entropyBits) + " bits";
}

void SchmidtView::drawBody(DrawContext& ctx) {
    if (!spectrum_ || x_.empty())
        return widgets::placeholder(ctx, note_.empty() ? "No Schmidt spectrum" : note_);
    const VizTheme& theme = *ctx.theme;
    const PlotStyle style(theme);
    const glm::vec2 body = bodySize();
    const ImVec2 bodyOrigin = ImGui::GetCursorScreenPos();
    if (ImPlot::BeginPlot("##schmidt", ImVec2(body.x, body.y),
                          ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("k", "λ_k", kCategoryAxis,
                          log_ ? ImPlotAxisFlags_AutoFit : ImPlotAxisFlags_AutoFit);
        if (log_)
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
        ImPlot::SetupAxisLimits(ImAxis_X1, -0.75, static_cast<double>(x_.size()) - 0.25,
                                ImPlotCond_Always);
        ImPlot::SetNextFillStyle(toImVec4(glm::vec3(theme.accent), 0.9f));
        ImPlot::PlotBars("λ", x_.data(), y_.data(), static_cast<int>(x_.size()), 0.7);
        const ImVec2 p0 = ImPlot::GetPlotPos(), sz = ImPlot::GetPlotSize();
        plot_ = {p0.x - bodyOrigin.x, p0.y - bodyOrigin.y, p0.x - bodyOrigin.x + sz.x,
                 p0.y - bodyOrigin.y + sz.y};
        ImPlot::EndPlot();
    }
}

} // namespace qlab::viz
