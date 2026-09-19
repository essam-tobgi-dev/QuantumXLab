#pragma once
// Spec 14 §10 — a small state-vector kernel for the equivalence checker. `qxl_compiler` links
// qxl_ir, qxl_hardware and qxl_pulse only (CMakeLists), so the checker cannot use qsim's
// backends; it needs nothing beyond gate application and projective measurement.
#include "Core/Random.hpp"
#include "IR/Node.hpp"
#include "Numerics/Types.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace qlab::compiler::detail {

class StateVec {
  public:
    explicit StateVec(std::uint32_t qubits); // |0…0⟩
    std::uint32_t qubits() const { return n_; }
    std::size_t dim() const { return amp_.size(); }
    std::vector<num::Complex>& amplitudes() { return amp_; }
    const std::vector<num::Complex>& amplitudes() const { return amp_; }

    void setBasis(std::size_t index);
    // Applies `u` (2^k × 2^k, wires[0] = least significant index) on the basis states whose control
    // bits match: bit set for a positive control, clear for a negative one.
    void apply(const num::Matrix& u, std::span<const std::uint32_t> wires,
               std::size_t controlMask = 0, std::size_t controlPattern = 0);
    // A gate node with its wires renamed through `wireMap` (node wire → state wire).
    Status applyGate(const ir::Gate& g, std::span<const std::uint32_t> wireMap);

    double probabilityOfOne(std::uint32_t wire) const;
    // Projects `wire` onto `outcome` and renormalises; returns the probability of that outcome.
    double project(std::uint32_t wire, int outcome);
    void flip(std::uint32_t wire); // X
    double norm() const;

  private:
    std::uint32_t n_;
    std::vector<num::Complex> amp_;
};

// Haar-random pure state on `qubits` qubits (normalised complex Gaussian vector), seeded.
std::vector<num::Complex> haarState(std::uint32_t qubits, core::Random& rng);
// ⟨a|b⟩.
num::Complex overlap(std::span<const num::Complex> a, std::span<const num::Complex> b);

} // namespace qlab::compiler::detail
