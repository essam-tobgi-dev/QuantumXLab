#pragma once
// Spec 14 §9 — scheduling: a start time for every node from calibrated durations, ASAP or ALAP, on
// the device's sample grid, with the critical path and the per-qubit idle intervals the noise
// model consumes (spec 08 §4).
#include "Compiler/Types.hpp"
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include "IR/Circuit.hpp"
#include <map>
#include <vector>

namespace qlab::compiler {

class PulseSource;

struct TimedNode {
    Picoseconds start{0}, duration{0};
    Picoseconds end() const { return start + duration; }
};
struct IdleInterval {
    Picoseconds start{0}, end{0};
    Picoseconds length() const { return end - start; }
    bool operator==(const IdleInterval&) const = default;
};

struct ScheduleInfo {
    SchedulePolicy policy = SchedulePolicy::Asap;
    Picoseconds dt{0};          // device sample period
    Picoseconds granule{0};     // dt × granularity_samples: every start and duration is a multiple
    Picoseconds duration{0};    // critical path T_circ = latest end time
    std::vector<TimedNode> nodes;                                 // parallel to `topologicalOrder()`
    // Per wire that carries an operation: the gaps between consecutive nodes, ascending (wires
    // without a gap are absent). Explicit `delay` nodes are nodes, not gaps, and the wait before a
    // wire's first node is not listed: the qubit is still in |0⟩ then.
    std::map<std::uint32_t, std::vector<IdleInterval>> idle;
    bool empty() const { return nodes.empty(); }
    Picoseconds totalIdle() const;
};

struct ScheduleOptions {
    SchedulePolicy policy = SchedulePolicy::Asap;
    bool enforceMaxDuration = true;     // QL4090 against `device.control.maxProgramDuration`
};

// Durations (spec 14 §9): per-qubit single-qubit and per-edge two-qubit gate durations, readout
// and reset durations from calibration and device timing; `rz` is 0 (virtual Z); `barrier` is 0
// but synchronises its wires; `delay[d]` is d (`dt` resolved with the device sample period);
// a Branch lasts the feedback latency plus its longer arm, a Loop the latency plus one pass of its
// body, a Box its declared duration (`err::BoxOverrun` when the body is longer). Calibrated values
// are rounded to the nearest point of the grid dt × granularity — the rule of `pulse::quantise`,
// so that a gate-level start time is a legal pulse start time — and delays are rounded up. With a
// `PulseSource` the durations are those of the pulse blocks themselves (they differ from the
// calibration table by at most one granule per pulse, and include readout acquisition windows).
//
// Writes each node's `duration`, rewrites delays in picoseconds, schedules nested bodies on their
// own time axis, and stores in `c.meta()["schedule"]`:
//   { "policy", "dt_ps", "granule_ps", "duration_ps", "start_ps": […], "length_ps": […],
//     "idle": { "<wire>": [[t_a, t_b], …] } }
Result<ScheduleInfo> schedule(ir::Circuit& c, const hw::Device& device, const hw::Calibration& calibration,
                              const ScheduleOptions& options = {}, const PulseSource* pulses = nullptr);

// QL4010: a `dt` duration cannot be resolved without a device (device-independent compiles).
Status requireResolvedDurations(const ir::Circuit& c);

} // namespace qlab::compiler
