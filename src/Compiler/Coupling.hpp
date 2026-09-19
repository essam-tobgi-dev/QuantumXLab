#pragma once
// Spec 14 §7–§8 — the coupling graph of a device as the layout and routing passes need it:
// adjacency over DATA qubits only (coupler qubits of tunable-coupler devices never hold program
// qubits), all-pairs hop distances, and reliability distances from calibration.
#include "Core/Error.hpp"
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace qlab::compiler {

class CouplingGraph {
  public:
    static constexpr std::uint32_t kUnreachable = std::numeric_limits<std::uint32_t>::max();

    // `calibration` may be null: edge costs are then uniform.
    static Result<CouplingGraph> build(const hw::Device& device,
                                       const hw::Calibration* calibration);

    std::uint32_t qubitCount() const { return qubits_; } // device qubits, couplers included
    const std::vector<std::uint32_t>& dataQubits() const { return data_; } // ascending
    bool isData(std::uint32_t q) const { return q < qubits_ && isData_[q] != 0; }
    const std::vector<std::uint32_t>& neighbours(std::uint32_t q) const { return adj_[q]; }
    std::size_t degree(std::uint32_t q) const { return adj_[q].size(); }
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>& edges() const {
        return edges_;
    } // a < b
    bool allToAll() const { return allToAll_; }
    bool hasCalibration() const { return calibrated_; }

    bool adjacent(std::uint32_t a, std::uint32_t b) const { return hops(a, b) == 1; }
    // BFS hop distance between data qubits; kUnreachable across components or for couplers.
    std::uint32_t hops(std::uint32_t a, std::uint32_t b) const { return hop_[a * qubits_ + b]; }
    // −ln F_2q of the edge itself (spec 14 §7 score), the mean edge cost where calibration is
    // missing, +∞ for a pair that is not coupled.
    double edgeCost(std::uint32_t a, std::uint32_t b) const;
    // Σ −ln F_2q along the most reliable path (spec 14 §8 "shortest reliability path").
    double pathCost(std::uint32_t a, std::uint32_t b) const { return rel_[a * qubits_ + b]; }
    // pathCost in units of the mean edge cost, so that it is comparable with `hops`.
    double reliabilityDistance(std::uint32_t a, std::uint32_t b) const {
        return rel_[a * qubits_ + b] / meanEdge_;
    }
    double meanEdgeCost() const { return meanEdge_; }
    std::uint32_t diameter() const { return diameter_; }

    // −ln F_readout and 1/T1 per qubit (0 without calibration).
    double readoutCost(std::uint32_t q) const { return q < readout_.size() ? readout_[q] : 0.0; }
    double decayRate(std::uint32_t q) const { return q < decay_.size() ? decay_[q] : 0.0; }
    double typicalTwoQubitSeconds() const { return typical2q_; }

  private:
    std::uint32_t qubits_ = 0;
    bool allToAll_ = false, calibrated_ = false;
    std::vector<std::uint32_t> data_;
    std::vector<std::uint8_t> isData_;
    std::vector<std::vector<std::uint32_t>> adj_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> edges_;
    std::vector<std::uint32_t> hop_;
    std::vector<double> rel_;
    std::vector<double> edgeCost_; // parallel to edges_
    std::vector<double> readout_, decay_;
    double meanEdge_ = 1.0, typical2q_ = 0.0;
    std::uint32_t diameter_ = 0;
};

} // namespace qlab::compiler
