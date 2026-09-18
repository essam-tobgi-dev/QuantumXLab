// Spec 21 §3.6 — Hinton diagram (see HintonView.hpp).
#include "Viz/Views/HintonView.hpp"
#include "Viz/Detail/ImGuiSupport.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Math/Phase.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>

namespace qlab::viz {
using namespace detail;

namespace {
constexpr float kLabelGutter = 46.0f; // room for ket labels on the left and bra labels on top
constexpr float kLegend = 96.0f;      // room for the phase wheel on the right
} // namespace

ReductionRequest HintonView::wants(const ViewInput& in) const {
    ReductionRequest r;
    const auto subset = qubitSubset();
    if (in.snapshot && !subset.empty() && subset.size() < in.snapshot->nQubits)
        r.subsets.emplace_back(subset.begin(), subset.end());
    return r;
}

void HintonView::rebuild(const ViewInput& in) {
    source_ = densityFor(in, qubitSubset());
    model_ = {};
    if (source_.rho)
        if (auto m = math::buildHinton(*source_.rho)) model_ = std::move(*m);
}

void HintonView::layout() {
    const glm::vec2 body = bodySize();
    const float side = std::max(0.0f, std::min(body.x - kLabelGutter - kLegend, body.y - kLabelGutter - 8.0f));
    cell_ = model_.dim > 0 ? side / static_cast<float>(model_.dim) : 0.0f;
    grid_ = {kLabelGutter, kLabelGutter, kLabelGutter + side, kLabelGutter + side};
}

Rect HintonView::cellRect(std::uint32_t row, std::uint32_t col) const {
    return {grid_.x0 + cell_ * static_cast<float>(col), grid_.y0 + cell_ * static_cast<float>(row),
            grid_.x0 + cell_ * static_cast<float>(col + 1), grid_.y0 + cell_ * static_cast<float>(row + 1)};
}

std::optional<HitResult> HintonView::hitTest(glm::vec2 local) const {
    if (model_.dim == 0 || cell_ <= 0.0f || !grid_.contains(local.x, local.y) || !source_.rho) return std::nullopt;
    const auto col = std::min<std::uint32_t>(static_cast<std::uint32_t>((local.x - grid_.x0) / cell_), static_cast<std::uint32_t>(model_.dim - 1));
    const auto row = std::min<std::uint32_t>(static_cast<std::uint32_t>((local.y - grid_.y0) / cell_), static_cast<std::uint32_t>(model_.dim - 1));
    const num::Complex v = (*source_.rho)(row, col);
    HitResult hit;
    hit.kind = HitKind::MatrixElement;
    hit.element = {row, col};
    hit.title = "rho[" + math::bitString(row, model_.nQubits) + ", " + math::bitString(col, model_.nQubits) + "]";
    const FidelityClass cls = input().stateClass();
    hit.readout.push_back({"value", math::formatCartesian(v), cls});
    hit.readout.push_back({"|rho_ij|", math::formatSig(std::abs(v)), cls});
    hit.readout.push_back({"arg", math::formatAngle(math::phaseOf(v)), cls}); // the hue, in words (spec 21 §4)
    return hit;
}

std::optional<std::string> HintonView::exportCsv() const {
    if (!source_.rho) return std::nullopt;
    std::string csv = "row,col,re,im\n";
    for (std::size_t i = 0; i < source_.rho->rows; ++i)
        for (std::size_t j = 0; j < source_.rho->cols; ++j) {
            const num::Complex v = (*source_.rho)(i, j);
            csv += math::bitString(i, model_.nQubits) + "," + math::bitString(j, model_.nQubits) + "," + math::formatSig(v.real(), 12) + "," +
                   math::formatSig(v.imag(), 12) + "\n";
        }
    return csv;
}

void HintonView::drawBody(DrawContext& ctx) {
    if (model_.dim == 0) return widgets::placeholder(ctx, source_.note.empty() ? "No density matrix to show" : source_.note);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const VizTheme& th = *ctx.theme;
    const auto at = [&](double x, double y) { return ImVec2(o.x + static_cast<float>(x), o.y + static_cast<float>(y)); };

    dl->AddRectFilled(at(grid_.x0, grid_.y0), at(grid_.x1, grid_.y1), toU32(th.bgRaised));
    if (cell_ >= 6.0f) // grid lines only while they do not drown the squares
        for (std::size_t k = 0; k <= model_.dim; ++k) {
            const float t = cell_ * static_cast<float>(k);
            dl->AddLine(at(grid_.x0 + t, grid_.y0), at(grid_.x0 + t, grid_.y1), toU32(th.border));
            dl->AddLine(at(grid_.x0, grid_.y0 + t), at(grid_.x1, grid_.y0 + t), toU32(th.border));
        }
    for (const math::HintonSquare& s : model_.squares) {
        const Rect c = cellRect(s.row, s.col);
        const double half = 0.5 * s.side * 0.92 * cell_; // area ∝ |ρ_ij|; 8 % margin keeps neighbours apart
        const ImVec2 p0 = at(c.cx() - half, c.cy() - half), p1 = at(c.cx() + half, c.cy() + half);
        dl->AddRectFilled(p0, p1, toU32(s.color));
        dl->AddRect(p0, p1, toU32(th.textSecondary, 0.8f)); // rim: the φ = 0 hue is near-black on the dark panel
    }
    // Ket labels (rows) and bra labels (columns) while they fit.
    if (cell_ >= ImGui::GetTextLineHeight())
        for (std::size_t k = 0; k < model_.dim; ++k) {
            const std::string bits = math::bitString(k, model_.nQubits);
            const ImVec2 sz = textSize(bits);
            const float mid = cell_ * (static_cast<float>(k) + 0.5f);
            drawText(dl, at(grid_.x0 - sz.x - 6.0f, grid_.y0 + mid - 0.5f * sz.y), th.textSecondary, bits);
            if (cell_ >= sz.x + 2.0f) drawText(dl, at(grid_.x0 + mid - 0.5f * sz.x, grid_.y0 - sz.y - 4.0f), th.textSecondary, bits);
        }
    const std::string caption = (source_.reduced ? "reduced rho, " : "rho, ") + subsetCaption(source_.qubits) + "   max |rho_ij| = " +
                                math::formatSig(model_.maxAbs);
    drawText(dl, at(grid_.x0, 4.0), th.textSecondary, caption);
    widgets::phaseWheel(ctx, {o.x + static_cast<float>(grid_.x1) + 0.5f * kLegend, o.y + static_cast<float>(grid_.y0) + 44.0f}, 22.0f);
    ImGui::Dummy(ImVec2(bodySize().x, bodySize().y));
}

} // namespace qlab::viz
