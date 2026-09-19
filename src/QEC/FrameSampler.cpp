// Spec 16 §4, §5.1; spec 07 §4 — Pauli-frame propagation through a Clifford schedule (T09 §1.3
// conjugation rules). The frame is the Pauli by which the faulty run differs from the fault-free
// one; a measurement record flips exactly when the frame has an X component on the measured qubit.
#include "QEC/Sampler.hpp"
#include <algorithm>

namespace qlab::qec {

FrameSampler::FrameSampler(const Schedule& schedule)
    : schedule_(schedule), x_(schedule.qubits, 0), z_(schedule.qubits, 0) {}

void FrameSampler::step(const Op& op, std::vector<std::uint8_t>* flips,
                        std::vector<std::uint32_t>* flipped) {
    const std::uint32_t a = op.a, b = op.b;
    switch (op.kind) {
    case OpKind::H:
        std::swap(x_[a], z_[a]);
        break; // X ↔ Z
    case OpKind::S:
    case OpKind::Sdg:
        z_[a] ^= x_[a];
        break; // X → ±Y, Z → Z
    case OpKind::CX:
        x_[b] ^= x_[a];
        z_[a] ^= z_[b];
        break; // X_c → X_c X_t, Z_t → Z_c Z_t
    case OpKind::CZ:
        z_[a] ^= x_[b];
        z_[b] ^= x_[a];
        break;       // X_a → X_a Z_b, X_b → Z_a X_b
    case OpKind::CY: // S_t · CX · S†_t
        z_[b] ^= x_[b];
        x_[b] ^= x_[a];
        z_[a] ^= z_[b];
        z_[b] ^= x_[b];
        break;
    case OpKind::Swap:
        std::swap(x_[a], x_[b]);
        std::swap(z_[a], z_[b]);
        break;
    case OpKind::Reset:
        x_[a] = 0;
        z_[a] = 0;
        break; // the reset discards the error
    case OpKind::Measure:
        if (x_[a] && op.bit != kNoIndex) {
            if (flips)
                (*flips)[op.bit] ^= 1u;
            if (flipped)
                flipped->push_back(op.bit);
        }
        break;
    case OpKind::X:
    case OpKind::Y:
    case OpKind::Z:
    case OpKind::Tick:
    case OpKind::RoundStart:
        break; // Paulis commute with the frame up to sign
    }
}

void FrameSampler::inject(const FaultEvent& f, std::vector<std::uint8_t>* flips) {
    auto add = [&](std::uint32_t q, char letter) {
        if (q >= schedule_.qubits)
            return;
        if (letter == 'X' || letter == 'Y')
            x_[q] ^= 1u;
        if (letter == 'Z' || letter == 'Y')
            z_[q] ^= 1u;
    };
    add(f.qubitA, f.pauliA);
    add(f.qubitB, f.pauliB);
    if (flips && f.flipBit < flips->size())
        (*flips)[f.flipBit] ^= 1u;
}

Status FrameSampler::run(std::span<const FaultEvent> faults, std::vector<std::uint8_t>& flips) {
    QXL_TRY(validateFaults(faults, schedule_));
    std::fill(x_.begin(), x_.end(), std::uint8_t{0});
    std::fill(z_.begin(), z_.end(), std::uint8_t{0});
    flips.assign(schedule_.bits, 0);
    if (faults.empty())
        return {};
    // The frame is empty until the first fault: start right after its operation.
    std::size_t next = 0;
    const std::uint32_t first = faults.front().afterOp;
    for (; next < faults.size() && faults[next].afterOp == first; ++next)
        inject(faults[next], &flips);
    for (std::size_t i = std::size_t(first) + 1; i < schedule_.ops.size(); ++i) {
        step(schedule_.ops[i], &flips, nullptr);
        for (; next < faults.size() && faults[next].afterOp == i; ++next)
            inject(faults[next], &flips);
    }
    return {};
}

void FrameSampler::propagate(std::uint32_t afterOp, std::uint32_t qubit, char pauli,
                             std::vector<std::uint32_t>& flippedBits) {
    std::fill(x_.begin(), x_.end(), std::uint8_t{0});
    std::fill(z_.begin(), z_.end(), std::uint8_t{0});
    flippedBits.clear();
    FaultEvent f;
    f.qubitA = qubit;
    f.pauliA = pauli;
    inject(f, nullptr);
    for (std::size_t i = std::size_t(afterOp) + 1; i < schedule_.ops.size(); ++i)
        step(schedule_.ops[i], nullptr, &flippedBits);
    // A bit written twice toggles twice: keep the bits with odd multiplicity, sorted.
    std::sort(flippedBits.begin(), flippedBits.end());
    std::vector<std::uint32_t> odd;
    for (std::size_t i = 0; i < flippedBits.size();) {
        std::size_t j = i;
        while (j < flippedBits.size() && flippedBits[j] == flippedBits[i])
            ++j;
        if ((j - i) % 2 == 1)
            odd.push_back(flippedBits[i]);
        i = j;
    }
    flippedBits = std::move(odd);
}

} // namespace qlab::qec
