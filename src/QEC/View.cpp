// Spec 16 §8 — plain-struct views of a code and of a decoded shot for the UI (spec 21 §3.12) and
// the chip role colouring (spec 17 §7).
#include "QEC/View.hpp"
#include <format>

namespace qlab::qec {
namespace {

QubitRole roleOf(CheckType t) {
    return t == CheckType::X ? QubitRole::AncillaX : (t == CheckType::Z ? QubitRole::AncillaZ : QubitRole::AncillaMixed);
}

void setCorrection(CodeView& view, const PauliString& pauli) {
    view.correction.assign(view.n, 'I');
    for (std::uint32_t q = 0; q < view.n && q < pauli.n; ++q) view.correction[q] = pauli.letter(q);
}

} // namespace

std::string_view roleName(QubitRole r) {
    switch (r) {
    case QubitRole::Data: return "data";
    case QubitRole::AncillaX: return "ancilla_x";
    case QubitRole::AncillaZ: return "ancilla_z";
    case QubitRole::AncillaMixed: return "ancilla_mixed";
    }
    return "data";
}

CodeView makeCodeView(const StabilizerCode& code) {
    CodeView v;
    v.codeId = code.id;
    v.n = code.n;
    v.checks = code.checkCount();
    v.distance = code.d;
    for (std::uint32_t q = 0; q < code.n; ++q)
        v.qubits.push_back({q, QubitRole::Data, q < code.dataLayout.size() ? code.dataLayout[q] : Coord2{}, kNoIndex, std::nullopt});
    for (std::uint32_t j = 0; j < v.checks && j < code.ancillas.size(); ++j) {
        const AncillaSpec& a = code.ancillas[j];
        const CheckType type = code.checkType(j);
        v.qubits.push_back({code.n + j, roleOf(type), a.coord, j, std::nullopt});
        for (std::uint32_t i = 0; i < a.order.size(); ++i) {
            const std::uint32_t q = a.order[i];
            if (q >= code.dataLayout.size()) continue;
            GateArrow arrow;
            arrow.check = j;
            arrow.dataQubit = q;
            // Surface codes run their checks in four global layers (spec 16 §2.1); elsewhere the step
            // is the position in the ancilla's own sequence.
            const int layer = code.family == CodeFamily::SurfaceRotated ? tomitaSvoreLayer(type, a.coord, code.dataLayout[q]) : -1;
            arrow.step = layer >= 0 ? static_cast<std::uint32_t>(layer) : i;
            arrow.letter = code.stabilizers[j].letter(q);
            arrow.ancillaControls = type != CheckType::Z;
            arrow.from = arrow.ancillaControls ? a.coord : code.dataLayout[q];
            arrow.to = arrow.ancillaControls ? code.dataLayout[q] : a.coord;
            v.arrows.push_back(arrow);
        }
    }
    for (const PauliString& l : code.logicalX) v.logicalX.push_back(l.support());
    for (const PauliString& l : code.logicalZ) v.logicalZ.push_back(l.support());
    v.syndrome.assign(v.checks, 0);
    v.correction.assign(v.n, 'I');
    return v;
}

void showSyndrome(CodeView& view, std::span<const std::uint8_t> syndrome, const Correction& correction) {
    view.syndrome.assign(view.checks, 0);
    for (std::uint32_t j = 0; j < view.checks && j < syndrome.size(); ++j) view.syndrome[j] = syndrome[j] & 1u;
    view.matched.clear();
    for (const MatchedEdge& e : correction.edges) view.matched.push_back({e.a, e.b});
    setCorrection(view, correction.pauli);
    view.cls = FidelityClass::Exact;
}

void showShot(CodeView& view, const MemoryExperiment& ex, const ShotResult& shot, std::uint32_t round) {
    view.syndrome.assign(view.checks, 0);
    if (round < ex.options.rounds)
        for (std::uint32_t j = 0; j < view.checks && j < ex.nAncilla; ++j) {
            const std::uint32_t bit = ex.syndromeBit(round, j);
            if (bit < shot.bits.size()) view.syndrome[j] = shot.bits[bit] & 1u;
        }
    view.matched.clear();
    auto checkOf = [&](std::uint32_t detector) { return detector < ex.detectors.size() ? ex.detectors[detector].check : kBoundary; };
    for (const MatchedEdge& e : shot.correction.edges) {
        const CheckPair pair{checkOf(e.a), e.b == kBoundary ? kBoundary : checkOf(e.b)};
        if (pair.a != pair.b) view.matched.push_back(pair);   // a time-like edge stays on one check
    }
    setCorrection(view, shot.correction.pauli);
    view.cls = FidelityClass::Statistical;
}

Status bindToChip(CodeView& view, std::span<const std::uint32_t> layout) {
    if (layout.size() < view.qubits.size())
        return fail(err::BadLayout, std::format("layout places {} wires, the code circuit has {}", layout.size(), view.qubits.size()));
    for (RoleEntry& r : view.qubits) r.physical = QubitIndex{layout[r.circuitQubit]};
    return {};
}

SpaceTimeLattice makeSpaceTimeLattice(const StabilizerCode& code, const MemoryExperiment& ex, const ShotResult& shot) {
    SpaceTimeLattice l;
    l.checks = ex.nAncilla;
    l.layers = ex.layers();
    for (std::uint32_t d = 0; d < ex.detectors.size(); ++d) {
        const Detector& det = ex.detectors[d];
        const bool fired = d < shot.events.size() && shot.events[d] != 0;
        l.points.push_back({d, det.check, det.layer, det.type, det.check < code.ancillas.size() ? code.ancillas[det.check].coord : Coord2{}, fired});
        l.events += fired ? 1u : 0u;
    }
    for (const MatchedEdge& e : shot.correction.edges) l.matched.push_back({e.a, e.b});   // point index = detector index
    return l;
}

} // namespace qlab::qec
