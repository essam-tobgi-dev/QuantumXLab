#pragma once
// Spec 08 §4–§5 — the noise model: calibrated parameters per qubit / gate / edge, the channels
// attached to each operation, the readout map, the Lindblad collapse operators and the JSON form
// `qlab.noise/1`. Built from a device + calibration (§4.1) or from JSON (§5).
//
// Deviations from the spec sketch (SPEC_DEVIATIONS.md): `channelsFor` takes the native gate name
// instead of `ir::Op` (Noise sits below IR, spec 02 §2); `fromCalibration` takes the calibration
// explicitly; `readout` returns Result; `lindbladOperators` takes the simulated qubits and their
// site dimensions and returns named full-dimension `qsim::CollapseOp`s.
#include "Core/Json.hpp"
#include "Core/Random.hpp"
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include "Noise/Drift.hpp"
#include "Noise/ModelTypes.hpp"
#include "QSim/SystemModel.hpp"
#include <filesystem>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace qlab::noise {

class NoiseModel {
  public:
    // ---- construction and persistence (spec 08 §4.1, §5)
    static Result<NoiseModel> fromCalibration(const hw::Device& device,
                                              const hw::Calibration& calibration,
                                              const NoiseOptions& options = {});
    static Result<NoiseModel> fromJson(const core::Json& data); // the envelope's `data` object
    static Result<NoiseModel> parse(const std::string& text);   // full envelope
    static Result<NoiseModel> load(const std::filesystem::path& file);
    core::Json toJson() const;
    std::string serialize() const; // core::JsonEnvelope, kind "qlab.noise", schema 1
    Status save(const std::filesystem::path& file) const;

    // ---- queries used by the compiler/runtime (spec 08 §4)
    // Channels of one native gate on physical qubits, in application order: thermal_relaxation
    // (After) and detuning_drift (During) for the gate duration, depolarizing, over_rotation,
    // leakage (After). "reset" yields the reset bit flip; "measure", "rz", "id", "barrier" and
    // "delay" yield nothing (readout error is classical, §3). An uncalibrated gate name falls back
    // to the model's default gate of that arity (spec 08 §8); a target tuple with no calibration
    // entry at all yields no gate channels.
    std::vector<AttachedChannel> channelsFor(std::string_view gate,
                                             std::span<const QubitIndex> qubits) const;
    // Relaxation and drift over an idle interval taken from the schedule (spec 08 §8).
    std::vector<AttachedChannel> idleChannels(QubitIndex q, double durationS) const;
    std::vector<AttachedChannel> idleChannels(QubitIndex q, Picoseconds dt) const;
    // ZZ crosstalk for one scheduled slot (During, spec 08 §2.4): every calibrated edge with at
    // least one endpoint idle or under a single-qubit gate; `inTwoQubitGates` lists the others.
    std::vector<AttachedChannel>
    crosstalkChannels(double durationS, std::span<const QubitIndex> inTwoQubitGates = {}) const;
    // Photon shot-noise dephasing of the unmeasured qubits on the feedlines being read (spec 08
    // §3).
    std::vector<AttachedChannel> measurementChannels(std::span<const QubitIndex> measured) const;
    // State-preparation error: thermal population left in |1⟩ at the start of a shot (T04 §10.3).
    std::vector<AttachedChannel> preparationChannels(std::span<const QubitIndex> qubits) const;
    // Assignment map of the measured qubits, bits in the given order (spec 08 §3).
    Result<ReadoutModel> readout(std::span<const QubitIndex> qubits) const;
    // Collapse operators for the Lindblad/Trajectories backends (spec 08 §7.4); site k simulates
    // qubits[k] with dimension siteDims[k]. Gate-error depolarizing is deliberately absent.
    Result<std::vector<qsim::CollapseOp>>
    lindbladOperators(std::span<const QubitIndex> qubits,
                      std::span<const std::uint32_t> siteDims) const;
    Result<std::vector<qsim::CollapseOp>>
    lindbladOperators(std::span<const std::uint32_t> siteDims) const; // site k = qubit k
    // One quasi-static detuning per qubit for a shot, δf_q ~ N(0, σ_f,q) in Hz (spec 08 §2.6).
    std::vector<double> drawShotDetunings(core::Random& rng) const;
    bool isPauliOnly() const; // Stabilizer-eligible without the Model-class twirl (spec 08 §7.3)

    // ---- data access
    std::size_t qubitCount() const { return qubits_.size(); }
    const QubitNoise* qubit(QubitIndex q) const;
    std::span<const QubitNoise> qubits() const { return qubits_; }
    const GateNoise* gate(std::string_view gateName, std::span<const QubitIndex> qubits) const;
    std::vector<const GateNoise*> gates() const;
    const EdgeNoise* edge(std::uint32_t a, std::uint32_t b) const;
    std::span<const ReadoutGroup> readoutGroups() const { return readoutGroups_; }
    std::span<const std::vector<std::uint32_t>> feedlines() const { return feedlines_; }
    std::span<const std::string> warnings() const { return warnings_; } // "warn: …" diagnostics
    std::uint32_t levels() const { return levels_; }
    const std::string& deviceId() const { return device_; }
    const std::string& calibrationStamp() const { return calibrationStamp_; }
    const Overrides& overrides() const { return overrides_; }
    // Re-derive every channel from the stored calibration; the model is unchanged on error.
    Status setOverrides(const Overrides& o);
    Status setLinePhotonNumbers(std::span<const double> photons); // cryo coupling for the next run

  private:
    static constexpr double kAbsent = std::numeric_limits<double>::infinity();
    // Stored values in the units of the `qlab.noise/1` document so a JSON round trip is bit-exact.
    // `extra` keeps unknown fields, which are written back (spec 23 §1).
    struct QubitRecord {
        double t1Us = kAbsent, t2Us = kAbsent, t2StarUs = kAbsent;
        double frequencyGhz = 0.0, pThermal = 0.0, linePhotons = 0.0;
        double resetError = 0.0, readoutDurationNs = 0.0, readoutDephasing = 0.0;
        Assignment2 assignment{{{1.0, 0.0}, {0.0, 1.0}}};
        core::Json extra = core::Json::object(), readoutExtra = core::Json::object();
    };
    struct GateRecord {
        std::vector<std::uint32_t> qubits;
        double error = 0.0, durationNs = 0.0, coherentFraction = 0.0, leakage = 0.0;
        std::optional<double> seepage; // defaults to leakage (spec 08 §4.1)
        core::Json extra = core::Json::object();
    };
    struct EdgeRecord {
        std::uint32_t a = 0, b = 0;
        double zzHz = 0.0;
        core::Json extra = core::Json::object();
    };
    struct QubitChannels {
        ChannelPtr relaxation, drift, preparation, reset, measurement;
    };
    struct GateEntry {
        GateNoise noise;
        ChannelPtr depolarizing, overRotation, leakage;
    };
    struct EdgeEntry {
        EdgeNoise noise;
        ChannelPtr zz;
    };
    // Everything `rebuild` derives, committed only when complete.
    struct Derived {
        std::vector<QubitNoise> qubits;
        std::vector<QubitChannels> channels;
        std::map<std::string, std::map<std::string, GateEntry>> gates;
        std::map<std::string, EdgeEntry> edges;
        std::vector<std::string> warnings;
    };

    // JSON reader sections (ModelJsonRead.cpp); each names the failing field path.
    Status readQubits(const core::Json& data);
    Status readGates(const core::Json& data);
    Status readReadout(const core::Json& data);
    Status readOverrides(const core::Json& data);

    Status rebuild();
    Result<Derived> derive(const Overrides& o) const;
    Status deriveQubits(const Overrides& o, Derived& out) const;
    Status deriveGates(const Overrides& o, Derived& out) const;
    Status buildChannels(Derived& out) const;
    void commit(Derived&& d);
    const GateEntry* findGate(std::string_view gateName, std::span<const QubitIndex> qubits) const;
    static std::string targetKey(std::span<const std::uint32_t> qubits);
    static std::string edgeKey(std::uint32_t a, std::uint32_t b); // "min-max"

    std::string device_, calibrationStamp_;
    std::uint32_t levels_ = 2;
    std::vector<QubitRecord> rawQubits_;
    std::map<std::string, std::map<std::string, GateRecord>>
        rawGates_;                               // gate → target key "0" / "0-1"
    std::map<std::string, EdgeRecord> rawEdges_; // file key "a-b"
    std::vector<ReadoutGroup> readoutGroups_;
    std::vector<core::Json> readoutGroupExtra_; // parallel to readoutGroups_
    std::vector<std::vector<std::uint32_t>> feedlines_;
    std::string fallback1q_,
        fallback2q_; // default gate per arity for uncalibrated names (spec 08 §8)
    Overrides overrides_;
    core::Json extra_ = core::Json::object(), sourceExtra_ = core::Json::object(),
               overridesExtra_ = core::Json::object();

    std::vector<QubitNoise> qubits_;
    std::vector<QubitChannels> qubitChannels_;
    std::map<std::string, std::map<std::string, GateEntry>> gates_;
    std::map<std::string, EdgeEntry> edges_;
    std::vector<std::string> warnings_;
};

} // namespace qlab::noise
