// Spec 21 §3.10 — reduced-state cards (see ReducedView.hpp).
#include "Viz/Views/ReducedView.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Detail/ImGuiSupport.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Views/StateSource.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz {
using namespace detail;

namespace {
constexpr float kCardPadding = 8.0f;
constexpr float kMinCardW = 210.0f;
constexpr float kMinCardH = 150.0f;
} // namespace

ReductionRequest ReducedView::wants(const ViewInput&) const {
    ReductionRequest r;
    r.singles = true;
    r.qubits.assign(qubitSubset().begin(), qubitSubset().end());
    return r;
}

const ReducedView::Card* ReducedView::card(QubitIndex q) const {
    for (const Card& c : cards_)
        if (c.qubit == q) return &c;
    return nullptr;
}

void ReducedView::rebuild(const ViewInput& in) {
    cards_.clear();
    note_.clear();
    const std::uint32_t n = in.qubitCount();
    if (n == 0) {
        note_ = "Run a program to see the reduced states";
        return;
    }
    for (QubitIndex q : shownQubits(n)) {
        Card c;
        c.qubit = q;
        if (const auto s = singleReductionFor(in, q, kInlineQubits)) {
            c.valid = true;
            c.rho = s->rho;
            c.r = s->bloch;
            c.purity = s->purity;
            c.entropyBits = s->entropyBits;
            c.leakage = s->leakage;
        }
        // Calibration of the physical qubit, when the device carries one (spec 21 §3.10).
        if (in.calibration)
            if (const hw::QubitCal* cal = in.calibration->qubit(q.get())) {
                c.t1S = cal->t1.value.v;
                c.t2S = cal->t2echo.value.v;
                c.f01Hz = cal->f01.value.v;
                c.gateError1q = cal->gateError1q.value;
                c.readoutError = 1.0 - cal->readoutFidelity();
            }
        cards_.push_back(std::move(c));
    }
    if (!in.snapshot) note_ = "Run a program to see the reduced states";
}

void ReducedView::layout() {
    const glm::vec2 body = bodySize();
    if (cards_.empty() || body.x <= 0.0f || body.y <= 0.0f) {
        columns_ = 1;
        return;
    }
    columns_ = std::max<std::size_t>(1, static_cast<std::size_t>(body.x / kMinCardW));
    columns_ = std::min(columns_, cards_.size());
    const std::size_t rows = (cards_.size() + columns_ - 1) / columns_;
    cardW_ = body.x / static_cast<float>(columns_);
    cardH_ = std::max(kMinCardH, std::min(body.y / static_cast<float>(rows), 2.0f * kMinCardH));
    for (std::size_t k = 0; k < cards_.size(); ++k) {
        const std::size_t r = k / columns_, c = k % columns_;
        const float x = static_cast<float>(c) * cardW_, y = static_cast<float>(r) * cardH_;
        cards_[k].rect = {x, y, x + cardW_, y + cardH_};
    }
}

std::optional<HitResult> ReducedView::hitTest(glm::vec2 local) const {
    for (const Card& c : cards_) {
        if (!c.rect.contains(local.x, local.y)) continue;
        HitResult h;
        h.kind = HitKind::Qubit;
        h.qubit = c.qubit;
        h.title = "q" + std::to_string(c.qubit.get());
        const data::FidelityClass cls = input().stateClass();
        if (c.valid) {
            h.readout.push_back({"r", "(" + math::formatSig(c.r.x) + ", " + math::formatSig(c.r.y) + ", " +
                                          math::formatSig(c.r.z) + ")",
                                 cls});
            h.readout.push_back({"|r|", math::formatSig(c.r.norm()), cls});
            h.readout.push_back({"purity", math::formatSig(c.purity), cls});
            h.readout.push_back({"S", math::formatSig(c.entropyBits) + " bits", cls});
            h.readout.push_back({"p₁", math::formatSig(c.r.p1()), cls});
            if (c.leakage > 1e-9) h.readout.push_back({"leakage", math::formatSig(c.leakage), cls});
        }
        if (c.t1S) h.readout.push_back({"T₁", math::formatTime(*c.t1S), data::FidelityClass::Model});
        if (c.t2S) h.readout.push_back({"T₂", math::formatTime(*c.t2S), data::FidelityClass::Model});
        if (c.f01Hz) h.readout.push_back({"f₀₁", math::formatFrequency(*c.f01Hz), data::FidelityClass::Model});
        if (c.readoutError)
            h.readout.push_back({"readout error", math::formatProbability(*c.readoutError), data::FidelityClass::Model});
        return h;
    }
    return std::nullopt;
}

std::optional<std::string> ReducedView::exportCsv() const {
    if (cards_.empty()) return std::nullopt;
    std::string csv = "qubit,rx,ry,rz,purity,entropy_bits,p1,leakage,t1_s,t2_s,f01_hz,readout_error\n";
    for (const Card& c : cards_) {
        csv += std::to_string(c.qubit.get()) + "," + math::formatSig(c.r.x, 12) + "," + math::formatSig(c.r.y, 12) + "," +
               math::formatSig(c.r.z, 12) + "," + math::formatSig(c.purity, 12) + "," +
               math::formatSig(c.entropyBits, 12) + "," + math::formatSig(c.r.p1(), 12) + "," +
               math::formatSig(c.leakage, 12) + ",";
        csv += (c.t1S ? math::formatSig(*c.t1S, 12) : std::string()) + "," +
               (c.t2S ? math::formatSig(*c.t2S, 12) : std::string()) + "," +
               (c.f01Hz ? math::formatSig(*c.f01Hz, 12) : std::string()) + "," +
               (c.readoutError ? math::formatSig(*c.readoutError, 12) : std::string()) + "\n";
    }
    return csv;
}

void ReducedView::drawBody(DrawContext& ctx) {
    if (cards_.empty()) return widgets::placeholder(ctx, note_.empty() ? "No qubit selected" : note_);
    const VizTheme& theme = *ctx.theme;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lh = ImGui::GetTextLineHeight();

    for (const Card& c : cards_) {
        const bool selected = ctx.selection && ctx.selection->isSelected(c.qubit);
        const ImVec2 p0(origin.x + static_cast<float>(c.rect.x0) + 3.0f, origin.y + static_cast<float>(c.rect.y0) + 3.0f);
        const ImVec2 p1(origin.x + static_cast<float>(c.rect.x1) - 3.0f, origin.y + static_cast<float>(c.rect.y1) - 3.0f);
        dl->AddRectFilled(p0, p1, toU32(theme.bgRaised), theme.radiusMd);
        dl->AddRect(p0, p1, toU32(selected ? theme.accent : theme.border), theme.radiusMd, 0, selected ? 2.0f : 1.0f);
        float y = p0.y + kCardPadding;
        drawText(dl, ImVec2(p0.x + kCardPadding, y), theme.qubitColor(c.qubit.get()), "q" + std::to_string(c.qubit.get()));
        y += lh + 2.0f;
        if (!c.valid) {
            drawText(dl, ImVec2(p0.x + kCardPadding, y), theme.textDisabled, "no reduced state");
            continue;
        }
        // ρ_k as a 2 × 2 matrix with bracket rules (spec 20 §3 renders the same layout in the UI).
        const float mx = p0.x + kCardPadding + 6.0f;
        for (std::size_t i = 0; i < 2 && c.rho.rows >= 2; ++i) {
            std::string line;
            for (std::size_t j = 0; j < 2; ++j) line += (j ? "   " : "") + math::formatCartesian(c.rho(i, j), 3);
            drawText(dl, ImVec2(mx, y + static_cast<float>(i) * lh), theme.textPrimary, line);
        }
        const float bracketTop = y - 1.0f, bracketBot = y + 2.0f * lh + 1.0f;
        dl->AddLine(ImVec2(mx - 5.0f, bracketTop), ImVec2(mx - 5.0f, bracketBot), toU32(theme.textSecondary));
        y = bracketBot + 4.0f;

        const auto line = [&](std::string_view label, const std::string& value, const glm::vec4& col) {
            drawText(dl, ImVec2(p0.x + kCardPadding, y), theme.textSecondary, label);
            drawText(dl, ImVec2(p0.x + kCardPadding + 86.0f, y), col, value);
            y += lh;
        };
        line("r", "(" + math::formatSig(c.r.x, 3) + ", " + math::formatSig(c.r.y, 3) + ", " + math::formatSig(c.r.z, 3) + ")",
             theme.textPrimary);
        line("|r|", math::formatSig(c.r.norm()), theme.textPrimary);
        line("purity", math::formatSig(c.purity), theme.textPrimary);
        line("p₁", math::formatSig(c.r.p1()), theme.textPrimary);
        if (c.t1S) line("T₁", math::formatTime(*c.t1S), theme.textPrimary);
        if (c.t2S) line("T₂", math::formatTime(*c.t2S), theme.textPrimary);
        if (c.f01Hz) line("f₀₁", math::formatFrequency(*c.f01Hz), theme.textPrimary);
        if (c.readoutError) line("readout err", math::formatProbability(*c.readoutError), theme.textPrimary);
    }
    ImGui::Dummy(ImVec2(bodySize().x, cardH_ * static_cast<float>((cards_.size() + columns_ - 1) / columns_)));
}

} // namespace qlab::viz
