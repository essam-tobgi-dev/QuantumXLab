// Spec 21 §3.1 — Bloch view: model, pagination, projection and hit testing (no GL, no ImGui here).
#include "Viz/Views/BlochView.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Math/Phase.hpp"
#include "Viz/Views/StateSource.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace qlab::viz {
namespace {

// ρ_k from, in order: the run's reductions, the reduced states the backend attached to the
// snapshot, or — for small registers only — the state itself (shared with the reduced-state cards).
std::optional<SingleReduction> reductionFor(const ViewInput& in, QubitIndex q) {
    return singleReductionFor(in, q, BlochView::kInlineReductionQubits);
}

} // namespace

ReductionRequest BlochView::wants(const ViewInput&) const {
    ReductionRequest r;
    r.singles = true;
    r.qubits.assign(qubitSubset().begin(), qubitSubset().end());
    return r;
}

const BlochView::Cell* BlochView::cell(QubitIndex q) const {
    for (const Cell& c : cells_)
        if (c.qubit == q)
            return &c;
    return nullptr;
}

void BlochView::rebuild(const ViewInput& in) {
    std::map<std::uint32_t, std::deque<glm::dvec3>> trails;
    for (Cell& c : cells_)
        trails[c.qubit.get()] = std::move(c.trail);
    cells_.clear();
    canvas_.invalidate();
    if (!in.snapshot) {
        haveLast_ = false;
        return;
    }
    const qsim::Snapshot& snap = *in.snapshot;
    // A rewind (playhead dragged back, new run) starts the trails over.
    const bool rewound = haveLast_ && (snap.gateIndex < lastGate_ || snap.simTimePs < lastTimePs_);
    const bool advanced =
        !haveLast_ || snap.gateIndex != lastGate_ || snap.simTimePs != lastTimePs_;
    for (QubitIndex q : shownQubits(snap.nQubits)) {
        Cell c;
        c.qubit = q;
        if (!rewound)
            c.trail = std::move(trails[q.get()]);
        if (const auto s = reductionFor(in, q)) {
            c.valid = true;
            c.r = s->bloch;
            c.purity = s->purity;
            c.entropyBits = s->entropyBits;
            c.leakage = s->leakage;
            // One trail point per snapshot, only for a reduction that belongs to this snapshot.
            if (advanced && !in.reductionsStale()) {
                c.trail.emplace_back(c.r.x, c.r.y, c.r.z);
                while (c.trail.size() > trailLength_)
                    c.trail.pop_front();
            }
        }
        cells_.push_back(std::move(c));
    }
    if (advanced && !in.reductionsStale()) {
        lastGate_ = snap.gateIndex;
        lastTimePs_ = snap.simTimePs;
        haveLast_ = true;
    }
}

void BlochView::layout() {
    canvas_.invalidate();
    const glm::vec2 body = bodySize();
    const std::size_t count = cells_.size();
    for (Cell& c : cells_)
        c.onPage = false;
    if (count == 0 || body.x <= 0.0f || body.y <= 0.0f) {
        pages_ = 1;
        page_ = 0;
        return;
    }
    constexpr float kMinCellPx = 120.0f;
    const std::size_t cols = std::min<std::size_t>(kPerRow, count);
    const std::size_t rowsFit =
        std::max<std::size_t>(1, static_cast<std::size_t>(body.y / kMinCellPx));
    perPage_ = cols * rowsFit;
    pages_ = (count + perPage_ - 1) / perPage_;
    page_ = std::min(page_, pages_ - 1);
    const std::size_t first = page_ * perPage_, last = std::min(count, first + perPage_);
    const std::size_t rows = (last - first + cols - 1) / cols;
    const float cellPx =
        std::min(body.x / static_cast<float>(cols), body.y / static_cast<float>(rows));
    const float x0 = 0.5f * (body.x - cellPx * static_cast<float>(cols));
    const float y0 = 0.5f * (body.y - cellPx * static_cast<float>(rows));
    for (std::size_t k = first; k < last; ++k) {
        Cell& c = cells_[k];
        const std::size_t i = k - first, row = i / cols, col = i % cols;
        c.onPage = true;
        c.rect = {x0 + cellPx * static_cast<float>(col), y0 + cellPx * static_cast<float>(row),
                  x0 + cellPx * static_cast<float>(col + 1),
                  y0 + cellPx * static_cast<float>(row + 1)};
        c.centerPx = {static_cast<float>(c.rect.cx()),
                      static_cast<float>(c.rect.cy()) - 0.04f * cellPx};
        c.radiusPx = 0.33f * cellPx;
    }
}

void BlochView::setPage(std::size_t page) {
    page_ = std::min(page, pages_ > 0 ? pages_ - 1 : 0);
    markLayoutDirty();
    setBodySize(bodySize());
}

void BlochView::setTrailLength(std::size_t n) {
    trailLength_ = std::max<std::size_t>(1, n);
    for (Cell& c : cells_)
        while (c.trail.size() > trailLength_)
            c.trail.pop_front();
    canvas_.invalidate();
}

void BlochView::setRotation(double yaw, double pitch) {
    yaw_ = yaw;
    pitch_ = std::clamp(pitch, -1.5, 1.5);
    canvas_.invalidate();
}

void BlochView::resetCamera() {
    setRotation(-0.45, 0.32);
}

void BlochView::onSelectionChanged(const SelectionModel& sel) {
    canvas_.invalidate();
    const auto q =
        sel.primaryQubit(); // spec 21 §1.1: the Bloch panel scrolls to the selected qubit
    if (!q)
        return;
    for (std::size_t k = 0; k < cells_.size(); ++k)
        if (cells_[k].qubit == *q && perPage_ > 0 && k / perPage_ != page_)
            setPage(k / perPage_);
}

glm::dmat3 BlochView::rotation() const {
    const double cy = std::cos(yaw_), sy = std::sin(yaw_), cp = std::cos(pitch_),
                 sp = std::sin(pitch_);
    const glm::dmat3 ry(cy, 0.0, -sy, 0.0, 1.0, 0.0, sy, 0.0, cy); // about world +y (column-major)
    const glm::dmat3 rx(1.0, 0.0, 0.0, 0.0, cp, sp, 0.0, -sp, cp); // about world +x
    return rx * ry;
}

glm::vec2 BlochView::projectPoint(const Cell& c, const glm::dvec3& p, double* depth) const {
    const glm::dvec3 w = rotation() * quantumToWorld(p);
    if (depth)
        *depth = w.z; // the orthographic camera looks down −z: larger z is nearer
    return {c.centerPx.x + c.radiusPx * static_cast<float>(w.x),
            c.centerPx.y - c.radiusPx * static_cast<float>(w.y)};
}

std::optional<HitResult> BlochView::hitTest(glm::vec2 local) const {
    for (const Cell& c : cells_) {
        if (!c.onPage || !c.rect.contains(local.x, local.y))
            continue;
        HitResult hit;
        hit.kind = HitKind::Qubit;
        hit.qubit = c.qubit;
        hit.title = "q" + std::to_string(c.qubit.get());
        const FidelityClass cls = input().stateClass();
        if (!c.valid) {
            hit.readout.push_back({"state", "reduction pending", cls});
            return hit;
        }
        hit.readout.push_back({"r",
                               "(" + math::formatSig(c.r.x) + ", " + math::formatSig(c.r.y) + ", " +
                                   math::formatSig(c.r.z) + ")",
                               cls});
        hit.readout.push_back({"|r|", math::formatSig(c.r.norm()), cls});
        hit.readout.push_back({"S(rho)", math::formatWithUnit(c.entropyBits, "bit"), cls});
        hit.readout.push_back({"theta", math::formatAngle(c.r.theta()), cls});
        hit.readout.push_back({"phi", math::formatAngle(c.r.phi()), cls});
        hit.readout.push_back({"p1", math::formatSig(c.r.p1()), cls});
        hit.readout.push_back({"Tr rho^2", math::formatSig(c.purity), cls});
        if (c.leakage > 1e-9)
            hit.readout.push_back({"leakage", math::formatSig(c.leakage), cls});
        return hit;
    }
    return std::nullopt;
}

} // namespace qlab::viz
