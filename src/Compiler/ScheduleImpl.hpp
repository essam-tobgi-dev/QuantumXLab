#pragma once
// Spec 14 §9 — node durations from calibration, or from the pulse blocks when a `PulseSource` is
// at hand. Internal to the Compiler module.
#include "Compiler/Schedule.hpp"
#include <map>
#include <string>

namespace qlab::compiler::detail {

struct Durations {
    Durations(const hw::Device& device, const hw::Calibration& calibration, const PulseSource* pulses);

    Result<Picoseconds> gate(const ir::Gate& g);
    Result<Picoseconds> measure(std::uint32_t qubit, const SourceSpan& span);
    Result<Picoseconds> reset(std::uint32_t qubit, const SourceSpan& span);
    // Calibrated values go to the nearest grid point (the rule of `pulse::quantise`, so that the
    // gate-level duration equals the pulse's); requested waits are rounded up.
    Picoseconds nearest(double seconds) const;
    Picoseconds ceilToGrid(Picoseconds p) const;

    const hw::Device& device;
    const hw::Calibration& calibration;
    const PulseSource* pulses;
    Picoseconds dt, granule, latency;
    int granularity;

private:
    Error missing(std::string_view what, std::initializer_list<std::uint32_t> qubits, const SourceSpan& span) const;   // QL4080
    std::map<std::string, Picoseconds> blockCache_;   // unparametrised pulse blocks by "gate|q0,q1"
};

} // namespace qlab::compiler::detail
