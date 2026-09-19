#pragma once
// Spec 09 §6 — frequency-collision checks for transmon devices (T05 §12).
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include <string>
#include <vector>

namespace qlab::hw {

enum class CollisionKind {
    Degenerate,
    Straddle,
    TwoPhoton,
    CrTooSlow,
    CrUnstable,
    Spectator,
    NeighbourDetuning
};
std::string_view collisionName(CollisionKind k);
// The six rules of spec 09 §6 are hard (the pair cannot be operated as specified);
// NeighbourDetuning is the device's own frequency-plan guideline and is advisory.
bool isHardCollision(CollisionKind k);

struct Collision {
    CollisionKind kind;
    std::uint32_t a, b;                   // the pair involved (for Spectator: target, spectator)
    std::optional<std::uint32_t> control; // CR control when relevant
    units::Frequency detuning;            // measured quantity compared against the threshold
    units::Frequency threshold;
    std::string message; // fidelity class Model
    bool hard = true;    // false for the advisory plan guideline
};

struct CollisionThresholds {
    units::Frequency degenerate{17e6};
    units::Frequency straddle{4e6};
    units::Frequency twoPhoton{30e6};
    units::Frequency crMax{200e6};
    units::Frequency crMin{30e6};
    units::Frequency spectator{17e6};
};

class FrequencyPlan {
  public:
    FrequencyPlan(const Device& dev, const Calibration& cal, CollisionThresholds th = {});
    // Evaluates all rules on coupled pairs and pairs sharing a neighbour. Empty for ion devices.
    std::vector<Collision> check() const;
    // Only the spec 09 §6 rules (excludes the advisory plan guideline).
    std::vector<Collision> hardCollisions() const;
    // Frequencies (Hz) per qubit index from the calibration; NaN for couplers without a calibration
    // entry.
    std::vector<double> frequencies() const;

  private:
    const Device& dev_;
    const Calibration& cal_;
    CollisionThresholds th_;
    void checkPair(std::uint32_t i, std::uint32_t j, bool coupled,
                   std::vector<Collision>& out) const;
};

} // namespace qlab::hw
