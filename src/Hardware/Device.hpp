#pragma once
// Spec 09 §2 — static device description (technology, qubits, coupling graph, gates, timing).
#include "Core/Error.hpp"
#include "Core/StrongType.hpp"
#include "Units/Units.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace qlab::hw {

enum class Technology { TransmonFixed, TransmonTunable, TransmonTunableCoupler, IonChain };
std::string_view technologyName(Technology t);
Result<Technology> technologyFromName(std::string_view s);
bool isTransmon(Technology t);

enum class QubitKind { Data, Coupler, Ancilla };
enum class EdgeKind { FixedCapacitive, TunableCoupler, AllToAll };
enum class ResetPolicy { Active, Passive, Cooling };

struct QubitInfo {
    std::uint32_t index = 0;
    QubitKind kind = QubitKind::Data;
    std::array<double, 2> pos{0.0, 0.0}; // layout coordinates in qubit pitches
};

struct EdgeInfo {
    std::uint32_t a = 0, b = 0;   // for AllToAll both are 0 and `allToAll` is set on the Device
    EdgeKind kind = EdgeKind::FixedCapacitive;
    bool directed = false;        // control→target (cross-resonance)
    std::optional<std::uint32_t> coupler; // coupler qubit index for TunableCoupler edges
};

struct NativeGateSet {
    std::vector<std::string> single; // e.g. x, sx, rz, id
    std::vector<std::string> two;    // e.g. cx, ecr | cz, siswap | ms
    std::string measure;             // dispersive | fluorescence
    std::string reset;               // measure_conditional_x | optical_pumping
    bool hasSingle(std::string_view g) const;
    bool hasTwo(std::string_view g) const;
};

struct Timing {
    std::int64_t dtPs = 222;
    int granularitySamples = 16;
    int minPulseSamples = 64;
    units::Time readout{640e-9};
    units::Time readoutRingdown{160e-9};
    units::Time repetitionDelay{250e-6};
    units::Time feedforward{200e-9};
};

struct ControlTiming {
    units::Time loadTime{50e-3};
    units::Time repOverhead{1e-6};
    units::Time feedbackLatency{200e-9};
    ResetPolicy resetPolicy = ResetPolicy::Active;
    std::optional<units::Time> activeResetDuration; // null → readout + feedback + x duration
    double passiveMultiplier = 5.0;
    units::Time coolingTime{1.5e-3};               // ions
    units::Time maxProgramDuration{10e-3};
    units::Time qecCycleTime{1e-6};
};

struct Feedline { int id = 0; std::vector<std::uint32_t> qubits; };

struct ReadoutConfig {
    std::vector<units::Frequency> resonatorFrequencies; // per qubit index (transmons)
    std::vector<Feedline> feedlines;
    bool purcellFilter = true;
    units::Frequency resonatorSpacingMin{40e6};
    // Ion fluorescence readout (spec 09 §4.5 / T06 §7)
    std::string method = "dispersive";
    units::Time detectionWindow{0.0};
    double collectionEfficiency = 0.0;
    double brightRatePerUs = 0.0, darkRatePerUs = 0.0;
};

struct FrequencyPlanConfig {
    units::Frequency bandLow{4.6e9}, bandHigh{5.4e9};
    units::Frequency minNeighbourDetuning{50e6};
    std::optional<std::pair<units::Frequency, units::Frequency>> couplerBand;
};

struct MotionalModes {
    std::string axis = "axial";
    int cutoff = 8;
    double heatingQuantaPerS = 50.0;
    units::Frequency omegaZ{0.3e6};  // axial COM (ordinary frequency)
    units::Frequency omegaR{3.0e6};  // radial
};

struct IonInfo {
    std::string species = "171Yb+";
    std::string qubit = "hyperfine_clock";
    units::Frequency fQubit{12.642812118e9};
    units::Length ramanWavelength{355e-9};
    double lambDickeNominal = 0.08;
};

struct DriveCrosstalk { std::uint32_t from = 0, to = 0; double amplitudeRatio = 0.0; double phase = 0.0; };

struct Device {
    std::string id;
    std::string displayName;
    Technology technology = Technology::TransmonFixed;
    int quditDimension = 3;
    std::vector<QubitInfo> qubits;
    std::vector<EdgeInfo> edges;
    bool allToAll = false;
    NativeGateSet gates;
    Timing timing;
    ControlTiming control;
    ReadoutConfig readout;
    FrequencyPlanConfig frequencyPlan;
    std::optional<MotionalModes> motionalModes;
    std::optional<IonInfo> ion;
    std::vector<DriveCrosstalk> crosstalk;
    bool leakageChannels = false;
    std::filesystem::path directory; // where it was loaded from (for pulses/wiring)

    // ---- graph queries (spec 09 §2; spec 14 routing uses these)
    std::size_t qubitCount() const { return qubits.size(); }
    std::size_t dataQubitCount() const;
    bool hasQubit(std::uint32_t q) const { return q < qubits.size(); }
    bool isCoupler(std::uint32_t q) const;
    // Adjacent in either direction (couplers are not adjacent to anything; edges are between data qubits).
    bool adjacent(std::uint32_t a, std::uint32_t b) const;
    // Index into `edges` for the pair (either order), or nullopt. AllToAll devices return 0 for any distinct pair.
    std::optional<std::size_t> edgeIndex(std::uint32_t a, std::uint32_t b) const;
    // True when the edge is directed and (control=a, target=b) is its native direction.
    bool nativeDirection(std::uint32_t control, std::uint32_t target) const;
    std::vector<std::uint32_t> neighbours(std::uint32_t q) const;
    std::size_t degree(std::uint32_t q) const { return neighbours(q).size(); }
    // BFS hop distance over the coupling graph; returns -1 when disconnected.
    int distance(std::uint32_t a, std::uint32_t b) const;
    std::vector<std::vector<int>> distanceMatrix() const; // data qubits only in index space of all qubits
    std::vector<std::uint32_t> dataQubits() const;
    std::optional<int> feedlineOf(std::uint32_t q) const;

    // ---- timing helpers (spec 09 §2 field rules)
    units::Time activeResetDuration(units::Time xDuration) const;

    // Structural validation: indices, edge endpoints, native gate names allowed for the technology,
    // duplicate edges, coupler references. Collects every violation.
    Result<void> validate() const;
};

// Native gate sets fixed by the spec per technology (a device may drop but not add).
const NativeGateSet& allowedGates(Technology t);

} // namespace qlab::hw
