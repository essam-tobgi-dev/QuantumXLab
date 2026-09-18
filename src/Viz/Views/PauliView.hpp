#pragma once
// Spec 21 §3.11 — Pauli expectation table `pauli` (Simulator-only, ImGui table + sparklines).
// ⟨P⟩ for every single-qubit Pauli and for user-added strings typed MSB-first ("XZIY"), each with a
// sparkline of its history over snapshots. Exact from a state or density snapshot; from shots, the
// estimator with σ = √((1 − ⟨P⟩²)/N) when the program measured every qubit of supp P in the basis
// P names — otherwise the row reads "not measured".
#include "Viz/Math/PauliTable.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/StateView.hpp"
#include <deque>

namespace qlab::viz {

class PauliView final : public StateView {
public:
    static constexpr std::size_t kHistory = 64;          // sparkline window, as the Bloch trail
    // Single-qubit rows are 3n; the O(2^n) expectation per row is affordable here only this small.
    static constexpr std::uint32_t kInlineQubits = 14;

    struct Row {
        math::PauliRow data;
        std::deque<double> history;                      // oldest first, one point per snapshot
        bool measured = true;                            // false: shots exist but not in this basis
    };

    std::string_view id() const override { return "pauli"; }
    std::string_view title() const override { return "Pauli expectations"; }
    Observability observability() const override { return Observability::SimulatorOnly; }
    Backend backend() const override { return Backend::Table; }
    std::string_view theoryAnchor() const override { return "T01 §4"; }
    ReductionRequest wants(const ViewInput& in) const override;
    data::FidelityClass fidelity(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    std::optional<std::string> exportCsv() const override;

    // User strings (spec 21 §3.11): MSB-first, padded on the low qubits. Returns the parse error.
    Status addPauli(std::string_view text);
    void removePauli(std::string_view text);
    std::span<const std::string> userPaulis() const { return user_; }
    std::span<const Row> rows() const { return rows_; }
    const Row* row(std::string_view label) const;
    float rowHeight() const { return rowHeight_; }

protected:
    void rebuild(const ViewInput& in) override;
    void layout() override;
    void drawBody(DrawContext& ctx) override;

private:
    void push(Row& row, std::optional<double> value);
    std::vector<Row> rows_;
    std::vector<std::string> user_;
    std::string note_;
    std::uint32_t nQubits_ = 0;
    float rowHeight_ = 0.0f, headerHeight_ = 0.0f;
};

} // namespace qlab::viz
