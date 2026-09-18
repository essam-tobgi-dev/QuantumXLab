#pragma once
// Spec 21 §1 — what the views consume. Deviation (SPEC_DEVIATIONS.md): Viz does not depend on
// Runtime, so instead of `runtime::RunSnapshot` the App layer fills this narrow record from the
// session: an immutable engine snapshot, the reductions computed for it off the UI thread
// (`computeReductions`), shot counts, the circuit at each compile stage, the pulse schedule with
// its playhead, the device with its calibration, time-domain series and QEC view data.
// Everything is held by shared pointer to const: a view keeps what it was given until the next
// `update()`, whatever the run does meanwhile.
#include "Data/Histogram.hpp"
#include "Data/Fidelity.hpp"
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include "IR/Circuit.hpp"
#include "Pulse/Schedule.hpp"
#include "QEC/View.hpp"
#include "QSim/Lindblad.hpp"
#include "QSim/Trajectories.hpp"
#include "QSim/Types.hpp"
#include "Viz/Math/PauliTable.hpp"
#include "Viz/Reductions.hpp"
#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace qlab::viz {

// Compile stages the circuit diagram can show (spec 19 §3 "Circuit Diagram").
enum class CircuitStage : std::uint8_t { Source, Decomposed, Routed, Scheduled };
inline constexpr std::size_t kCircuitStageCount = 4;
constexpr std::string_view circuitStageName(CircuitStage s) {
    switch (s) {
    case CircuitStage::Source: return "Source";
    case CircuitStage::Decomposed: return "Decomposed";
    case CircuitStage::Routed: return "Routed";
    case CircuitStage::Scheduled: return "Scheduled";
    }
    return "?";
}

struct CircuitSet {
    std::array<std::shared_ptr<const ir::Circuit>, kCircuitStageCount> stages; // null = stage not produced
    std::vector<std::uint32_t> layout;       // virtual → physical at the start (`ir::Circuit::layout()`)
    std::vector<std::uint32_t> finalLayout;  // after routing ("final_layout" of the routed circuit)
    const std::shared_ptr<const ir::Circuit>& at(CircuitStage s) const { return stages[static_cast<std::size_t>(s)]; }
    // The most compiled stage present, or null.
    std::shared_ptr<const ir::Circuit> latest() const;
};

// Lindblad run as a time series (spec 21 §3.15). `samples[k].populations` is site-major
// [site][level]. `purity` and `blochNorm[site]` are optional second-axis curves, parallel to samples.
struct LindbladSeries {
    std::uint32_t sites = 0, levels = 2;
    std::vector<qsim::TimeSample> samples;
    std::vector<double> purity;
    std::vector<std::vector<double>> blochNorm;
    data::FidelityClass cls = data::FidelityClass::Numerical;
};

// Monte-Carlo trajectory run (spec 21 §3.16) for one observable ⟨Z_k⟩(t).
struct TrajectoryTrace {
    std::vector<double> timeS, z;
    std::vector<double> jumpTimesS;
};
struct TrajectoryEnsemble {
    std::uint32_t qubit = 0;
    std::vector<TrajectoryTrace> traces;     // the view draws at most 256 of them
    std::vector<double> timeS, meanZ, stderrZ; // ensemble mean over ALL trajectories with σ/√N
    std::uint64_t trajectories = 0;          // N of the mean (may exceed traces.size())
    data::FidelityClass cls = data::FidelityClass::Statistical;
};
// Mean and standard error of ⟨Z⟩ = P_0 − P_1 of `site` from the backend's averaged samples.
TrajectoryEnsemble ensembleFromAverages(std::span<const qsim::AveragedSample> samples, std::uint32_t site,
                                        std::uint32_t levels, std::uint64_t trajectories);

struct ViewInput {
    // ---- state (Simulator-only)
    std::shared_ptr<const qsim::Snapshot> snapshot;          // null before the first run
    std::shared_ptr<const Reductions> reductions;            // may lag the snapshot: views mark it stale
    // ---- shots (Physical)
    std::shared_ptr<const data::Histogram> counts;
    std::shared_ptr<const std::vector<double>> idealProbabilities; // exact Born distribution over the classical bits
    data::FidelityClass idealClass = data::FidelityClass::Exact;   // Exact (state vector) or Numerical (density matrix)
    std::shared_ptr<const math::MeasurementMap> measurement; // which qubit was read in which basis into which bit
    // ---- program
    CircuitSet circuits;
    std::uint64_t playheadGate = 0;                          // index in topological order of the shown stage
    bool hasPlayhead = false;
    // ---- pulses
    std::shared_ptr<const pulse::Schedule> schedule;
    double playheadS = 0.0;                                  // Lindblad snapshot time (spec 21 §3.14)
    // ---- device
    std::shared_ptr<const hw::Device> device;
    std::shared_ptr<const hw::Calibration> calibration;
    // ---- time-domain runs
    std::shared_ptr<const LindbladSeries> lindblad;
    std::shared_ptr<const TrajectoryEnsemble> trajectories;
    // ---- error correction (spec 16 §8)
    std::shared_ptr<const qec::CodeView> code;
    std::shared_ptr<const qec::SpaceTimeLattice> lattice;

    // Fidelity class of the state data: the snapshot's class (Exact / Numerical / Statistical).
    data::FidelityClass stateClass() const { return snapshot ? snapshot->cls : data::FidelityClass::Exact; }
    // True when `reductions` were computed for an older snapshot than `snapshot`.
    bool reductionsStale() const;
    std::uint32_t qubitCount() const;                        // snapshot, else device, else circuit
};

// Non-owning shared pointer to an object that outlives the views (device assets, test fixtures).
template <class T> std::shared_ptr<const T> borrow(const T& object) {
    return std::shared_ptr<const T>(std::shared_ptr<void>{}, &object);
}

} // namespace qlab::viz
