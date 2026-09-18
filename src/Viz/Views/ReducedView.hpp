#pragma once
// Spec 21 §3.10 — reduced-state cards `reduced` (Simulator-only, ImGui draw list). One card per
// selected qubit: ρ_k as a 2 × 2 matrix, the Bloch vector, purity, p₁ = (1 − r_z)/2 and — when the
// device has calibration — that qubit's T₁, T₂, f₀₁ and readout assignment error, so the state and
// the hardware that produced it are read side by side. Clicking a card selects the qubit, which is
// what links it to the Inspector (spec 21 §1.1).
#include "Viz/Math/Reduced.hpp"
#include "Viz/StateView.hpp"

namespace qlab::viz {

class ReducedView final : public StateView {
public:
    // ρ_k is O(2^n) per qubit: computed here only for registers this small, else asked of the run.
    static constexpr std::uint32_t kInlineQubits = 12;

    struct Card {
        QubitIndex qubit{0};
        bool valid = false;
        num::Matrix rho;                 // 2 × 2, computational block
        math::BlochVector r;
        double purity = 1.0, entropyBits = 0.0, leakage = 0.0;
        // Calibration of this qubit, when the device has one (spec 21 §3.10).
        std::optional<double> t1S, t2S, f01Hz, readoutError, gateError1q;
        Rect rect;                       // body-local ImGui units
    };

    std::string_view id() const override { return "reduced"; }
    std::string_view title() const override { return "Reduced states"; }
    Observability observability() const override { return Observability::SimulatorOnly; }
    Backend backend() const override { return Backend::DrawList; }
    std::string_view theoryAnchor() const override { return "T01 §5"; }
    ReductionRequest wants(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;

    std::span<const Card> cards() const { return cards_; }
    const Card* card(QubitIndex q) const;
    std::size_t columns() const { return columns_; }

protected:
    void rebuild(const ViewInput& in) override;
    void layout() override;
    void drawBody(DrawContext& ctx) override;

private:
    std::vector<Card> cards_;
    std::string note_;
    std::size_t columns_ = 1;
    float cardW_ = 0.0f, cardH_ = 0.0f;
};

} // namespace qlab::viz
