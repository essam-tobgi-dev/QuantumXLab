#pragma once
// Spec 10 §6 — the gate→pulse calibration table (`pulses.json`): frames, parametrised
// templates, and one defcal per (native gate, qubits) of the device.
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include "Pulse/Expr.hpp"
#include "Pulse/Physics.hpp"
#include "Pulse/Schedule.hpp"
#include <filesystem>
#include <initializer_list>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace qlab::pulse {

struct DefcalKey {
    std::string gate;
    std::vector<std::uint32_t> qubits;
    auto operator<=>(const DefcalKey&) const = default;
    std::string toString() const;
};

// One entry of the table. Either explicit `instructions` or a `ref` to a template.
struct Defcal {
    DefcalKey key;
    std::vector<std::string> params; // formal parameter names, e.g. {"theta"}
    core::Json instructions;         // array, may be null when `ref` is used
    std::string ref;                 // template name
    std::string paramsFrom;          // "cal.edges.0-1"
    core::Json extra;                // the raw defcal object (amp_cr, flux_channel, …)
};

// Mølmer–Sørensen parameters of one `ms(θ)` invocation (spec 10 §6.6, T06 §6).
// The loop-closure and phase conditions are imposed on the envelope actually played (the
// `gaussian_square` with calibrated edges, sampled at dt): δ solves α(τ) = 0 of T06 (11.1) and Ω
// solves θ = −4Φ(τ) of T06 (6.2)/(6.5). For a square envelope these reduce to spec 10 §6.6,
// δτ = 2πK and Ω = √(θδ/(η_i η_j τ)); `omegaSquareRadPerS` is that value at |θ| = π/2 and must
// match the table's `omega_rad_s_at_pi_2`.
struct MsParams {
    int loops = 1;                      // K
    double durationS = 0.0;             // τ (on the device grid)
    double edgeS = 0.0;                 // gaussian_square rise/fall
    double detuningRadPerS = 0.0;       // δ, sign −sign(θ) (T06 (6.5))
    double squareDetuningRadPerS = 0.0; // |δ| = 2πK/τ of a square envelope
    double omegaRadPerS = 0.0;          // per-tone carrier Rabi rate realising θ
    double omegaMaxRadPerS = 0.0;       // the same at |θ| = π/2 (unit envelope amplitude)
    double omegaSquareRadPerS = 0.0;    // square-envelope Ω at |θ| = π/2 (spec 10 §6.6, T06 (6.6))
    double etaI = 0.0, etaJ = 0.0;      // Lamb–Dicke factors of the two ions on the gate mode
    double theta = 0.0;                 // requested XX angle, XX(θ) = exp(−iθ/2 XX)
    double amplitude = 0.0;             // envelope scale = Ω/Ω_max = √(|θ|/(π/2))
    std::string modeAxis = "axial";
    int modeIndex = 0;
    double modeFrequencyHz = 0.0;
};

// Echoed cross-resonance layout of one `cx`/`ecr` invocation (spec 10 §6.3, T05 §8).
struct CrParams {
    std::uint32_t control = 0, target = 0;
    Picoseconds crDuration{0};        // T_CR, one half
    Picoseconds echoDuration{0};      // T_x, the echo π pulse on the control
    Picoseconds totalDuration{0};     // 2·T_CR + 2·T_x = calibrated gate length on the grid
    double calibratedDurationS = 0.0; // cal.edges.c-t.duration_ns, before quantisation
    double ampCr = 0.0, ampCancel = 0.0, phaseCancel = 0.0;
    double sigmaS = 0.0, riseS = 0.0;
};

class PulseLibrary {
  public:
    // Loads `<dir>/pulses.json` against the device and calibration of the same directory
    // (hw::loadDevice), resolves every `cal.*` reference against the typed calibration, and
    // fails with E_NO_DEFCAL when a native gate on a qubit/edge has no defcal (spec 10 §6).
    static Result<PulseLibrary> load(const std::filesystem::path& deviceDir);
    static Result<PulseLibrary> fromJson(core::Json pulses, hw::Device device,
                                         hw::Calibration calibration);
    // The same table resolved against a new calibration (spec 09 §8 recalibration).
    Result<PulseLibrary> withCalibration(hw::Calibration calibration) const;

    const std::string& deviceId() const { return deviceId_; }
    const hw::Device& device() const { return device_; }
    const hw::Calibration& calibration() const { return calibration_; }
    const std::map<std::string, FrameDecl>& frames() const { return frames_; }
    // The frame `pulses.json` declares for a channel, if any.
    const FrameDecl* frameFor(ChannelId ch) const;
    std::vector<DefcalKey> keys() const;
    bool has(std::string_view gate, std::span<const std::uint32_t> qubits) const;
    // Exact (gate, qubits) match; the symmetric gates ms, cz and siswap also match reversed qubits.
    const Defcal* find(std::string_view gate, std::span<const std::uint32_t> qubits) const;
    // Spec 10 §6: every native gate on every data qubit and edge has a defcal. Lists all gaps.
    Result<void> checkComplete() const;
    // Every `ms` defcal's stored `omega_rad_s_at_pi_2` equals the square-envelope value of T06
    // (6.6) for this calibration (1e-6 relative). `load` runs it; a recalibrated library need not
    // pass.
    Result<void> checkMsTable() const;

    // Build the schedule for one gate invocation. `params` supplies the formal parameters
    // (e.g. theta for rz/rx/ms). `dt` comes from the device timing.
    Result<Schedule> scheduleFor(std::string_view gate, std::span<const std::uint32_t> qubits,
                                 const ParamMap& params = {}) const;
    Result<Schedule> scheduleFor(std::string_view gate, std::initializer_list<std::uint32_t> qubits,
                                 const ParamMap& params = {}) const;

    // κ_d [rad s⁻¹ per unit amplitude] for a qubit. Read from the calibration when present
    // (`drive_rad_per_s_per_V`), otherwise inferred from the `sx`/`x`/`rx`/`ry` defcal via the
    // area theorem (spec 10 §7) — the shipped calibrations do not carry the field.
    Result<double> driveCoupling(std::uint32_t qubit) const;
    // Rad/s per unit envelope amplitude of a Hamiltonian channel: κ_d of the driven qubit for
    // d[i], r[i] and u[c,t] (the control's line, spec 10 §4); Ω_max of the pair for ms[i,j].
    Result<double> rabiScale(ChannelId ch) const;

    // Resolved parameters of the two-qubit templates, for calibration and for tests.
    Result<MsParams> msParams(std::uint32_t i, std::uint32_t j, double theta) const;
    Result<CrParams> crParams(std::uint32_t control, std::uint32_t target) const;

    Picoseconds dt() const { return dt_; }
    int granularity() const { return granularity_; }
    int minPulseSamples() const { return minPulseSamples_; }
    bool isIonDevice() const { return device_.technology == hw::Technology::IonChain; }
    const core::Json& templates() const { return templates_; }
    // Resolver bound to this library's calibration; valid while the library is alive and unmoved.
    PathResolver resolver() const;
    // Quantise `seconds` onto the device grid (spec 10 §1).
    Picoseconds quantise(double seconds, bool isPlay = false) const;

    // Loop-closing detuning |δ| and phase integral J = ∫∫ e e' sin δ(t−t') of the unit MS
    // envelope for one duration (T06 (11.1), (6.2)); computed once per duration at load.
    struct MsShape {
        double durationS = 0.0, edgeS = 0.0;
        int loops = 1;
        double delta = 0.0, phaseIntegral = 0.0;
    };

  private:
    Result<Schedule> buildFromInstructions(const Defcal& d, const ParamMap& params) const;
    Result<Schedule> buildFromTemplate(const Defcal& d, const ParamMap& params) const;
    Result<Schedule> buildCrEcho(const Defcal& d, bool bare) const;
    Result<Schedule> buildFluxGate(const Defcal& d, std::string_view templateName) const;
    Result<Schedule> buildMs(const Defcal& d, const ParamMap& params) const;
    Result<Schedule> buildMeasure(const Defcal& d) const;
    Result<Schedule> buildResetActive(const Defcal& d) const;
    Result<double> path(std::string_view p) const;
    Result<Waveform> singleQubitWaveform(std::string_view gate, std::uint32_t q) const;
    Result<MsShape> msShape(double durationS) const;
    Result<void> prepareMsShapes();
    void attachFrames(Schedule& s) const;
    Schedule emptySchedule() const;

    std::string deviceId_;
    hw::Device device_;
    hw::Calibration calibration_;
    core::Json pulsesJson_;
    std::map<std::string, FrameDecl> frames_;
    core::Json templates_ = core::Json::object();
    std::map<DefcalKey, Defcal> defcals_;
    std::map<std::int64_t, MsShape> msShapes_; // keyed by duration in ps
    Picoseconds dt_{222};
    int granularity_ = 16;
    int minPulseSamples_ = 64;
};

// Spec 10 §6 — load `<deviceDir>/pulses.json` together with the device and calibration files
// it resolves `cal.*` / `device.*` references against.
Result<PulseLibrary> loadPulses(const std::filesystem::path& deviceDir);

} // namespace qlab::pulse
