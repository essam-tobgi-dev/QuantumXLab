#pragma once
// Spec 10 §8, spec 07 §5 — a schedule as the time-dependent drive terms of the Lindblad backend:
// piecewise-constant envelopes in rad/s, written in the simulation frame of their site, one per
// Hamiltonian channel. The operators they multiply come from the device model (hw::SystemModelSpec
// → qsim::SystemModel); `envelope()` is the callable a qsim::DriveTerm takes.
#include "Pulse/Frames.hpp"
#include "Pulse/Library.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace qlab::pulse {

enum class DriveKind : std::uint8_t {
    Rabi,               // d[i], r[i], u[c,t]
    SpinDependentForce, // ms[i,j]
    Flux,               // f[i]: dimensionless flux amplitude of pulses.json, real
};

struct DriveEnvelope {
    std::string channel;                 // "d[0]": the name hw::DriveSpec and qsim::DriveTerm carry
    ChannelId id;
    DriveKind kind = DriveKind::Rabi;
    std::uint32_t site = 0;              // device qubit whose frame the value is written in (control of u[c,t])
    std::optional<std::uint32_t> target; // CR target, or the second ion of ms[i,j]
    double dtS = 0.0;                    // hold period: the schedule dt (spec 10 §1)
    // scale·A·e(t_k)·e^{iφ(t_k)} held on [t_k, t_k + dt): φ the channel's phase instructions
    // (virtual Z); flux: A·e(t_k).
    std::vector<Complex> held;
    double scale = 1.0;                  // rad/s per unit envelope: κ_d (Rabi), Ω_max of the pair (MS), 1 (flux)
    double channelFrequencyHz = 0.0;     // declared carrier of the channel frame
    double referenceFrequencyHz = 0.0;   // frame the value is written in: site f01 (rwa) or 0 (lab)
    double qubitFrequencyHz = 0.0;       // carrier of the driven qubit (MS lab form)
    bool rwa = true;
    // MS at the first play (T06 §6.1): δ = ω_mode − μ with μ/2π the ms frame frequency.
    double detuningRadPerS = 0.0, modeFrequencyHz = 0.0, etaI = 0.0, etaJ = 0.0;
    std::shared_ptr<const FrameTimeline> frames;

    // The drive at time t [s], zero outside the schedule:
    //  Rabi, rwa:  Ω(t) = held_k·e^{−i2π∫₀ᵗ(f_ch − f_ref)dt'};  H = (Re Ω·X + Im Ω·Y)/2 on the site, in the
    //              frame at f_ref (T05 (7.1), T07 (1.2)). A CR tone thus oscillates at f_t − f_c (spec 10 §8.2).
    //  Rabi, lab:  Ω(t) = 2·Re[held_k·e^{−i2π∫₀ᵗ f_ch dt'}] (real);  H = Ω(t)·X/2.
    //  MS, rwa:    F(t) = 2·held_k·cos(2π∫₀ᵗ μ dt'), the bichromatic field on each ion (spins in their
    //              rotating frames, the mode in its lab frame as hw::buildIonModel keeps it):
    //              H = Σ_{n∈{i,j}} (η_n/2)(Re F σ_x⁽ⁿ⁾ + Im F σ_y⁽ⁿ⁾)(a + a†). Its interaction picture is
    //              T06 (6.1) with g = η|held|/2, S_φ at φ = arg held, detuning δ = ω_mode − μ.
    //  MS, lab:    2·Re[F(t)·e^{−i2π f_q t}] (real), multiplying Σ_n (η_n/2) σ_x⁽ⁿ⁾(a + a†).
    //  Flux:       held_k.
    Complex at(double tS) const;
    // Accumulated carrier phase 2π∫₀ᵗ f_ch dt' of the channel frame (for MS: of μ).
    double carrierPhase(double tS) const;
    std::function<Complex(double)> envelope() const;
};

// Zero-Hamiltonian parts of the schedule the backend acts on (spec 10 §8.5, §6.8).
struct ScheduleEvent {
    enum class Kind : std::uint8_t { ReadoutTone, Acquire, Detect, Pump };
    Kind kind = Kind::Acquire;
    ChannelId channel;
    std::uint32_t qubit = 0;  // index of m[i] / a[i]; 0 for the ion detect and pump beams
    double t0S = 0.0, durationS = 0.0;
    AcquireKind acquire = AcquireKind::Integrate;
    std::string weights;
    int memorySlot = 0;
};

struct DriveOptions {
    bool rwa = true;                     // false: lab frame at the full carrier (spec 10 §8.2)
    std::optional<double> sharedFrameHz; // one rotating frame for every site (hw::SystemModelOptions)
};

struct SystemDrives {
    double durationS = 0.0;
    double dtS = 0.0;
    bool rwa = true;
    std::vector<DriveEnvelope> drives;   // one per Hamiltonian channel with a Play, sorted by channel
    std::vector<ScheduleEvent> events;   // readout tones, acquisition windows, detection, pumping; by time
    const DriveEnvelope* find(std::string_view channel) const;
};

// Scales come from the library's calibration (PulseLibrary::rabiScale); the schedule must be on
// the library's dt. Errors: kErrDrive for a dt mismatch or a channel with no calibrated scale
// (the global Raman beam `g`), plus any library error.
Result<SystemDrives> toSystemDrives(const Schedule& schedule, const PulseLibrary& library,
                                    const DriveOptions& options = {});
// Loads the pulse library from `device.directory` (the files the device was loaded from).
Result<SystemDrives> toSystemDrives(const Schedule& schedule, const hw::Device& device, bool rwa = true);

} // namespace qlab::pulse
