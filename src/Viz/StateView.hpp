#pragma once
// Spec 21 §1, §4 — shared behaviour of every view: change detection (so `update` is O(1) when the
// input did not change and the §3 cost is paid once per snapshot), the stale flag, the header,
// hover readout after 150 ms, click → shared selection, keyboard (F, R, 1–9), qubit subset.
// A concrete view implements `rebuild` (model from the input), `layout` (geometry for the body
// size), `drawBody` and `hitTest`.
#include "Viz/IStateView.hpp"
#include "Data/Fidelity.hpp"
#include <array>
#include <vector>

namespace qlab::viz {

class StateView : public IStateView {
public:
    // ---- IStateView, shared
    void update(const ViewInput& in) final;
    void draw(DrawContext& ctx) final;
    bool stale() const override { return stale_; }
    void setBodySize(glm::vec2 size) final;
    glm::vec2 bodySize() const final { return bodySize_; }
    void setOnHover(HitCallback fn) final { onHover_ = std::move(fn); }
    void setOnClick(HitCallback fn) final { onClick_ = std::move(fn); }
    std::optional<HitResult> click(glm::vec2 local, SelectionModel* selection, bool additive = false) final;
    void setQubitSubset(std::span<const QubitIndex> qubits) final;
    std::span<const QubitIndex> qubitSubset() const final { return subset_; }
    std::string statusLine() const override;
    std::optional<std::string> exportCsv() const override { return std::nullopt; }
    void frameContent() override {}
    void resetCamera() override {}
    ReductionRequest wants(const ViewInput&) const override { return {}; }
    data::FidelityClass fidelity(const ViewInput& in) const override { return in.stateClass(); }

    // Number of times `rebuild` ran: tests use it to prove that an unchanged input costs nothing.
    std::uint64_t rebuildCount() const { return rebuilds_; }
    const ViewInput& input() const { return input_; }

protected:
    virtual void rebuild(const ViewInput& in) = 0;
    virtual void layout() {}
    virtual void drawBody(DrawContext& ctx) = 0;
    // Called after a click was applied to the selection (views with their own click semantics).
    virtual void onClicked(const HitResult&, SelectionModel*) {}
    // Selection changed elsewhere (lab pick, another view): highlight / scroll to the qubit.
    virtual void onSelectionChanged(const SelectionModel&) {}

    // A view option changed: rebuild on the next `update` (or now, when an input is present).
    void markDirty();
    void markLayoutDirty() { layoutDirty_ = true; }
    // The qubits the view shows: the subset when one is set (restricted to < n), else 0 … n−1.
    std::vector<QubitIndex> shownQubits(std::uint32_t nQubits) const;
    bool hasSnapshot() const { return static_cast<bool>(input_.snapshot); }
    void setStale(bool s) { stale_ = s; }

private:
    // Identity of everything a view may read; equal stamps mean nothing changed.
    struct Stamp {
        std::array<const void*, 16> pointers{};
        std::uint64_t playheadGate = 0;
        double playheadS = 0.0;
        bool hasPlayhead = false;
        bool operator==(const Stamp&) const = default;
    };
    static Stamp stampOf(const ViewInput& in);

    ViewInput input_;
    Stamp stamp_;
    bool haveInput_ = false, dirty_ = true, layoutDirty_ = true, stale_ = false;
    glm::vec2 bodySize_{0.0f, 0.0f};
    std::vector<QubitIndex> subset_;
    HitCallback onHover_, onClick_;
    std::uint64_t rebuilds_ = 0;
    std::uint64_t selectionRevision_ = 0;
    // hover timing (spec 21 §4: readout within 150 ms)
    std::string hoverKey_;
    double hoverSince_ = 0.0;
};

// Identity of a hit for hover timing: the same mark keeps its timer while the mouse moves on it.
std::string hitKey(const HitResult& hit);

} // namespace qlab::viz
