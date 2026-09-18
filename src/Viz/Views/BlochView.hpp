#pragma once
// Spec 21 §3.1 — Bloch sphere view `bloch` (Simulator-only, GL canvas). One sphere per selected
// qubit in a grid of 8 per row, paginated; the vector r = (Tr ρX, Tr ρY, Tr ρZ) as an arrow of
// length |r| with a mixedness disc at its tip, a fading trail of the last 64 snapshots, and the
// dashed drop-line to the z axis with p₁ = (1 − r_z)/2. |0⟩ is the north pole (H = −½ħωZ).
// Cost per update: O(1) per qubit — ρ_k comes from the run's reductions.
#include "Viz/GlCanvas.hpp"
#include "Viz/Math/Reduced.hpp"
#include "Viz/StateView.hpp"
#include <deque>

namespace qlab::viz {

class BlochView final : public StateView {
public:
    static constexpr std::size_t kTrailLength = 64;   // spec 21 §3.1 default
    static constexpr std::uint32_t kPerRow = 8;
    // Without reductions from the run, ρ_k is computed here only for registers this small (the
    // scan is 2^n per qubit; at n = 12 that is ≈ 50 µs for all qubits).
    static constexpr std::uint32_t kInlineReductionQubits = 12;

    struct Cell {
        QubitIndex qubit{0};
        bool valid = false;               // a ρ_k was available
        math::BlochVector r;
        double purity = 1.0, entropyBits = 0.0, leakage = 0.0;
        std::deque<glm::dvec3> trail;     // oldest first, quantum frame
        Rect rect;                        // cell in body-local ImGui units (current page only)
        glm::vec2 centerPx{0.0f};         // sphere centre, body-local
        float radiusPx = 0.0f;            // on-screen radius of the unit sphere
        bool onPage = false;
    };

    std::string_view id() const override { return "bloch"; }
    std::string_view title() const override { return "Bloch sphere"; }
    Observability observability() const override { return Observability::SimulatorOnly; }
    Backend backend() const override { return Backend::GlCanvas; }
    std::string_view theoryAnchor() const override { return "T01 §5"; }
    ReductionRequest wants(const ViewInput& in) const override;
    std::optional<HitResult> hitTest(glm::vec2 local) const override;
    void frameContent() override { resetCamera(); }
    void resetCamera() override;

    // ---- model access (tests, Inspector)
    std::span<const Cell> cells() const { return cells_; }
    const Cell* cell(QubitIndex q) const;
    std::size_t page() const { return page_; }
    std::size_t pageCount() const { return pages_; }
    void setPage(std::size_t page);
    void setTrailLength(std::size_t n);
    // View rotation applied to every sphere about its own centre (radians).
    void setRotation(double yaw, double pitch);
    // Where the quantum-frame point `p` of `cell` lands in the body (ImGui units), and its depth
    // toward the viewer (> 0 = front hemisphere).
    glm::vec2 projectPoint(const Cell& cell, const glm::dvec3& p, double* depth = nullptr) const;
    // Headless render of the current page to a PNG (spec 21 §4 export; the brief's render test).
    Status renderPng(GlBackend& gl, const VizTheme& theme, int widthPx, int heightPx, const std::filesystem::path& png);

protected:
    void rebuild(const ViewInput& in) override;
    void layout() override;
    void drawBody(DrawContext& ctx) override;
    void onSelectionChanged(const SelectionModel& sel) override;

private:
    glm::dmat3 rotation() const;
    GlCanvas::SceneFn scene(const VizTheme& theme, const SelectionModel* selection, float pxScale) const;
    void drawCell(gfx::Renderer& r, GlBackend& gl, const VizTheme& theme, const Cell& c, bool selected, float pxScale,
                  const glm::dvec3& centerWorld) const;
    std::vector<Cell> cells_;
    std::size_t page_ = 0, pages_ = 1, perPage_ = kPerRow;
    std::size_t trailLength_ = kTrailLength;
    double yaw_ = -0.45, pitch_ = 0.32;
    std::uint64_t lastGate_ = 0;
    double lastTimePs_ = -1.0;
    bool haveLast_ = false;
    mutable GlCanvas canvas_;
};

} // namespace qlab::viz
