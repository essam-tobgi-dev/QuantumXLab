// Spec 16 §4, §6; spec 07 §4 — the reference engine: a QEC schedule on the stabilizer backend with
// Pauli faults applied to the tableau between operations.
#include "QEC/Sampler.hpp"
#include <algorithm>
#include <format>

namespace qlab::qec {
namespace {

Status checkSchedule(const Schedule& s) {
    if (s.qubits == 0) return fail(err::BadOptions, "schedule has no qubits");
    for (std::size_t i = 0; i < s.ops.size(); ++i) {
        const Op& op = s.ops[i];
        if (op.marker()) continue;
        if (op.a >= s.qubits || (op.twoQubit() && (op.b >= s.qubits || op.b == op.a)))
            return fail(err::BadOptions, std::format("schedule op {} ({}) addresses a qubit outside 0..{}", i,
                                                     opName(op.kind), s.qubits - 1));
        if (op.kind == OpKind::Measure && op.bit != kNoIndex && op.bit >= s.bits)
            return fail(err::BadOptions, std::format("schedule op {} writes bit {} of {}", i, op.bit, s.bits));
    }
    return {};
}

void applyPauli(qsim::StabilizerBackend& st, std::uint32_t q, char letter) {
    if (letter == 'X') st.x(q);
    else if (letter == 'Y') st.y(q);
    else if (letter == 'Z') st.z(q);
}

} // namespace

Status validateFaults(std::span<const FaultEvent> faults, const Schedule& s) {
    std::uint32_t previous = 0;
    for (const FaultEvent& f : faults) {
        if (f.afterOp < previous || f.afterOp >= s.ops.size())
            return fail(err::BadOptions, "faults must be ordered by afterOp and refer to schedule operations");
        previous = f.afterOp;
        const bool badA = f.qubitA != kNoIndex && f.qubitA >= s.qubits, badB = f.qubitB != kNoIndex && f.qubitB >= s.qubits;
        if (badA || badB || (f.flipBit != kNoIndex && f.flipBit >= s.bits))
            return fail(err::BadOptions, std::format("fault after op {} addresses a qubit or bit outside the schedule", f.afterOp));
    }
    return {};
}

Result<TableauSampler> TableauSampler::create(const Schedule& schedule) {
    QXL_TRY(checkSchedule(schedule));
    TableauSampler t;
    t.schedule_ = schedule;
    QXL_TRY(t.pristine_.allocate(schedule.qubits));
    return t;
}

Status TableauSampler::run(std::span<const FaultEvent> faults, core::Random& rng, std::vector<std::uint8_t>& bits) const {
    qsim::StabilizerBackend state;
    return run(faults, rng, bits, state);
}

Status TableauSampler::run(std::span<const FaultEvent> faults, core::Random& rng, std::vector<std::uint8_t>& bits,
                           qsim::StabilizerBackend& st) const {
    QXL_TRY(validateFaults(faults, schedule_));
    st = pristine_;
    bits.assign(schedule_.bits, 0);
    std::vector<std::uint8_t> recordFlips(schedule_.bits, 0);
    std::size_t next = 0;
    for (std::uint32_t i = 0; i < schedule_.ops.size(); ++i) {
        const Op& op = schedule_.ops[i];
        const QubitIndex qa{op.a};
        switch (op.kind) {
        case OpKind::H: st.h(op.a); break;
        case OpKind::S: st.s(op.a); break;
        case OpKind::Sdg: st.sdg(op.a); break;
        case OpKind::X: st.x(op.a); break;
        case OpKind::Y: st.y(op.a); break;
        case OpKind::Z: st.z(op.a); break;
        case OpKind::CX: st.cnot(op.a, op.b); break;
        case OpKind::CZ: st.cz(op.a, op.b); break;
        case OpKind::CY: st.sdg(op.b); st.cnot(op.a, op.b); st.s(op.b); break;   // CY = S_t · CX · S†_t
        case OpKind::Swap: st.swap(op.a, op.b); break;
        case OpKind::Reset: QXL_TRY(st.reset(std::span<const QubitIndex>(&qa, 1), rng)); break;
        case OpKind::Measure: {
            QXL_TRY_ASSIGN(const qsim::Outcome out, st.measure(std::span<const QubitIndex>(&qa, 1), rng));
            if (op.bit != kNoIndex && !out.bits.empty()) bits[op.bit] = out.bits[0];
            break;
        }
        case OpKind::Tick:
        case OpKind::RoundStart: break;
        }
        for (; next < faults.size() && faults[next].afterOp == i; ++next) {
            const FaultEvent& f = faults[next];
            if (f.qubitA != kNoIndex) applyPauli(st, f.qubitA, f.pauliA);
            if (f.qubitB != kNoIndex) applyPauli(st, f.qubitB, f.pauliB);
            if (f.flipBit != kNoIndex) recordFlips[f.flipBit] ^= 1u;
        }
    }
    for (std::size_t b = 0; b < bits.size(); ++b) bits[b] ^= recordFlips[b];
    return {};
}

} // namespace qlab::qec
