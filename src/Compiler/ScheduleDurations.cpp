// Spec 14 §9, 09 §3–§4 — durations of gates, readout and reset.
#include "Compiler/PulseLower.hpp"
#include "Compiler/ScheduleImpl.hpp"
#include <format>

namespace qlab::compiler::detail {

Durations::Durations(const hw::Device& dev, const hw::Calibration& cal, const PulseSource* source)
    : device(dev), calibration(cal), pulses(source), dt(Picoseconds{dev.timing.dtPs}),
      granule(Picoseconds{dev.timing.dtPs * std::max(1, dev.timing.granularitySamples)}),
      latency(Picoseconds{0}), granularity(std::max(1, dev.timing.granularitySamples)) {
    latency = nearest(dev.control.feedbackLatency.si());
}

Picoseconds Durations::nearest(double seconds) const {
    return pulse::quantise(seconds, dt, granularity, 0);
}

Picoseconds Durations::ceilToGrid(Picoseconds p) const {
    if (p.get() <= 0)
        return Picoseconds{0};
    const std::int64_t g = granule.get();
    return Picoseconds{(p.get() + g - 1) / g * g};
}

Error Durations::missing(std::string_view what, std::initializer_list<std::uint32_t> qubits,
                         const SourceSpan& span) const {
    std::string where;
    for (std::uint32_t q : qubits)
        where += std::format("{}${}", where.empty() ? "" : ", ", q);
    return lang::Diagnostics::make("QL4080", span, what, where).error;
}

Result<Picoseconds> Durations::gate(const ir::Gate& g) {
    if (g.width() == 0)
        return Picoseconds{0}; // gphase
    const bool plain = g.controls.empty() && !g.adjoint && !g.custom;
    std::vector<std::uint32_t> qubits;
    for (ir::Wire w : g.wires())
        qubits.push_back(w.index);
    // Pulse-level timing: the block itself is the duration (program defcal or pulses.json).
    if (pulses && (pulses->library() || pulses->hasProgramDefcal(g.name, qubits))) {
        std::string key;
        if (g.params.empty()) {
            key = g.name + "|";
            for (std::uint32_t q : qubits)
                key += std::format("{},", q);
            if (auto it = blockCache_.find(key); it != blockCache_.end())
                return it->second;
        }
        QXL_TRY_ASSIGN(const pulse::Schedule block, pulses->gateBlock(g));
        const Picoseconds length = ceilToGrid(block.duration());
        if (!key.empty())
            blockCache_.emplace(std::move(key), length);
        return length;
    }
    if (plain && g.name == "rz")
        return Picoseconds{0}; // virtual Z (§5.4)
    if (g.opaque || !plain || qubits.size() > 2) {
        if (qubits.size() == 1)
            return fail(missing(g.name, {qubits[0]}, g.span));
        if (qubits.size() == 2)
            return fail(missing(g.name, {qubits[0], qubits[1]}, g.span));
        return fail(Error(err::Unsupported, std::format("gate '{}' on {} qubits has no calibrated "
                                                        "duration; decompose before scheduling",
                                                        g.name, qubits.size()))
                        .withSpan(g.span));
    }
    auto seconds = calibration.gateDuration(g.name, qubits);
    if (!seconds)
        return qubits.size() == 1 ? fail(missing(g.name, {qubits[0]}, g.span))
                                  : fail(missing(g.name, {qubits[0], qubits[1]}, g.span));
    return nearest(seconds->si());
}

Result<Picoseconds> Durations::measure(std::uint32_t q, const SourceSpan& span) {
    const std::uint32_t one[1] = {q};
    if (pulses && (pulses->library() || pulses->hasProgramDefcal("measure", one))) {
        QXL_TRY_ASSIGN(const pulse::Schedule block, pulses->measureBlock(q, span));
        return ceilToGrid(block.duration());
    }
    const hw::QubitCal* qc = calibration.qubit(q);
    if (!qc)
        return fail(missing("measure", {q}, span));
    return nearest(qc->readoutDuration.value.si());
}

Result<Picoseconds> Durations::reset(std::uint32_t q, const SourceSpan& span) {
    const std::uint32_t one[1] = {q};
    if (pulses && (pulses->library() || pulses->hasProgramDefcal("reset", one))) {
        QXL_TRY_ASSIGN(const pulse::Schedule block, pulses->resetBlock(q, span));
        return ceilToGrid(block.duration());
    }
    const hw::QubitCal* qc = calibration.qubit(q);
    if (!qc)
        return fail(missing("reset", {q}, span));
    switch (device.control.resetPolicy) {
    case hw::ResetPolicy::Active: // measure + feedback latency + conditional x (spec 09 §2)
        return nearest(device.activeResetDuration(qc->duration1q.value).si());
    case hw::ResetPolicy::Passive:
        return nearest(device.control.passiveMultiplier * qc->t1.value.si());
    case hw::ResetPolicy::Cooling:
        return nearest(device.control.coolingTime.si());
    }
    return Picoseconds{0};
}

} // namespace qlab::compiler::detail
