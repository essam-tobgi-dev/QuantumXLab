// Spec 21 §3.14 — pulse viewer layout (see PulseLayout.hpp).
#include "Viz/Layout/PulseLayout.hpp"
#include "Viz/Math/Phase.hpp"
#include "Pulse/Frames.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace qlab::viz::layout {
namespace {

constexpr double kNsPerS = 1e9;

// Row order of spec 21 §3.14 ("drive, flux, measure, acquire"), then by qubit index.
int kindOrder(pulse::ChannelKind k) {
    switch (k) {
    case pulse::ChannelKind::Drive: return 0;
    case pulse::ChannelKind::Control: return 1;
    case pulse::ChannelKind::Raman: return 1;
    case pulse::ChannelKind::GlobalRaman: return 2;
    case pulse::ChannelKind::Bichromatic: return 2;
    case pulse::ChannelKind::Flux: return 3;
    case pulse::ChannelKind::Pump: return 4;
    case pulse::ChannelKind::Measure: return 5;
    case pulse::ChannelKind::Detect: return 5;
    case pulse::ChannelKind::Acquire: return 6;
    }
    return 7;
}

bool selected(pulse::ChannelId ch, std::span<const QubitIndex> qubits) {
    if (qubits.empty()) return true;
    for (QubitIndex q : qubits) {
        if (ch.a == q.get()) return true;
        if (pulse::channelKindArity(ch.kind) == 2 && ch.b == q.get()) return true;
    }
    // Channels without an index (global beams) are never filtered out: they act on every ion.
    return pulse::channelKindArity(ch.kind) == 0;
}

// Accumulates the baseband envelope of one channel on the device sample grid. Overlap is rejected
// by Schedule::verify, so a plain write of each Play's samples reconstructs the channel exactly.
struct Accumulator {
    double dtNs = 0.0;
    std::map<std::int64_t, num::Complex> samples; // sample index → envelope
};

void addPlay(Accumulator& acc, const pulse::Play& p, std::int64_t dtPs) {
    const std::vector<num::Complex> wave = p.wf.sampled(dtPs);
    const std::int64_t start = p.t0.value / dtPs;
    for (std::size_t k = 0; k < wave.size(); ++k) acc.samples[start + static_cast<std::int64_t>(k)] += wave[k];
}

// Decimation of spec 22 §2: the window is split into `columns` bins; each bin keeps its mean and
// its extrema, so a fast oscillation reads as a band rather than an aliased line.
void decimate(PulseTrace& t, std::size_t columns) {
    const std::size_t n = t.tNs.size();
    if (n <= columns || columns < 2) return;
    PulseTrace out;
    out.decimated = true;
    out.sourceSamples = n;
    out.maxAbs = t.maxAbs;
    const double first = t.tNs.front(), last = t.tNs.back();
    const double span = last > first ? last - first : 1.0;
    std::size_t k = 0;
    for (std::size_t c = 0; c < columns && k < n; ++c) {
        const double edge = first + span * static_cast<double>(c + 1) / static_cast<double>(columns);
        const std::size_t begin = k;
        double sumR = 0.0, sumI = 0.0, loR = t.re[k], hiR = t.re[k], loI = t.im[k], hiI = t.im[k];
        while (k < n && (t.tNs[k] <= edge || k == begin)) {
            sumR += t.re[k];
            sumI += t.im[k];
            loR = std::min(loR, t.re[k]);
            hiR = std::max(hiR, t.re[k]);
            loI = std::min(loI, t.im[k]);
            hiI = std::max(hiI, t.im[k]);
            ++k;
        }
        const double count = static_cast<double>(k - begin);
        out.tNs.push_back(0.5 * (t.tNs[begin] + t.tNs[k - 1]));
        out.re.push_back(sumR / count);
        out.im.push_back(sumI / count);
        out.reLo.push_back(loR);
        out.reHi.push_back(hiR);
        out.imLo.push_back(loI);
        out.imHi.push_back(hiI);
    }
    t = std::move(out);
}

} // namespace

const PulseRow* PulseModel::row(pulse::ChannelId ch) const {
    for (const PulseRow& r : rows)
        if (r.channel == ch) return &r;
    return nullptr;
}

PulseModel buildPulseModel(const pulse::Schedule& schedule, const PulseLayoutOptions& options) {
    PulseModel model;
    const std::int64_t dtPs = std::max<std::int64_t>(1, schedule.dt().value);
    model.dtNs = static_cast<double>(dtPs) * 1e-3;
    model.durationNs = pulse::secondsOf(schedule.duration()) * kNsPerS;
    model.t0Ns = options.t0Ns;
    model.t1Ns = options.t1Ns > options.t0Ns ? options.t1Ns : std::max(model.durationNs, model.dtNs);

    const pulse::FrameTimeline frames(schedule);
    std::map<pulse::ChannelId, Accumulator> envelopes;
    std::map<pulse::ChannelId, PulseRow> rows;
    const auto rowFor = [&](pulse::ChannelId ch) -> PulseRow& {
        auto it = rows.find(ch);
        if (it == rows.end()) {
            PulseRow r;
            r.channel = ch;
            r.label = ch.toString();
            r.qubit = ch.primaryQubit();
            r.acquire = ch.kind == pulse::ChannelKind::Acquire;
            r.frequencyHz = frames.declaredFrequencyHz(ch);
            it = rows.emplace(ch, std::move(r)).first;
        }
        return it->second;
    };

    for (const pulse::Instruction& instr : schedule.instructions()) {
        const pulse::ChannelId ch = pulse::instructionChannel(instr);
        if (!selected(ch, options.qubits)) continue;
        if (const auto* play = std::get_if<pulse::Play>(&instr)) {
            rowFor(ch);
            addPlay(envelopes[ch], *play, dtPs);
        } else if (const auto* op = std::get_if<pulse::FrameOp>(&instr)) {
            PulseRow& r = rowFor(ch);
            const double tNs = pulse::secondsOf(op->t0) * kNsPerS;
            if (op->op == pulse::FrameOp::Op::SetPhase || op->op == pulse::FrameOp::Op::ShiftPhase) {
                const double after = frames.phaseOps(ch, pulse::secondsOf(op->t0));
                r.phaseJumps.push_back({tNs, math::wrapPhase(after),
                                        op->op == pulse::FrameOp::Op::ShiftPhase ? op->value : 0.0});
            } else {
                r.frequencyChanges.push_back({tNs, frames.frequencyHz(ch, pulse::secondsOf(op->t0))});
            }
        } else if (const auto* acq = std::get_if<pulse::Acquire>(&instr)) {
            PulseRow& r = rowFor(ch);
            r.acquisitions.push_back({pulse::secondsOf(acq->t0) * kNsPerS,
                                      pulse::secondsOf(acq->t0 + acq->length) * kNsPerS, acq->memorySlot});
        } else if (const auto* d = std::get_if<pulse::Delay>(&instr)) {
            rowFor(d->ch); // a channel that only idles still gets its row
        }
    }

    for (auto& [ch, row] : rows) {
        auto it = envelopes.find(ch);
        if (it != envelopes.end() && !it->second.samples.empty()) {
            const std::int64_t first = it->second.samples.begin()->first;
            const std::int64_t last = it->second.samples.rbegin()->first;
            PulseTrace& t = row.trace;
            t.sourceSamples = static_cast<std::size_t>(last - first + 1);
            t.tNs.reserve(t.sourceSamples);
            for (std::int64_t k = first; k <= last; ++k) {
                const auto s = it->second.samples.find(k);
                const num::Complex v = s == it->second.samples.end() ? num::Complex{} : s->second;
                t.tNs.push_back(static_cast<double>(k) * model.dtNs);
                t.re.push_back(v.real());
                t.im.push_back(v.imag());
                t.maxAbs = std::max(t.maxAbs, std::abs(v));
            }
            decimate(t, options.maxPoints);
            model.maxAbs = std::max(model.maxAbs, t.maxAbs);
        }
        std::sort(row.phaseJumps.begin(), row.phaseJumps.end(),
                  [](const FrameMark& a, const FrameMark& b) { return a.tNs < b.tNs; });
        std::sort(row.frequencyChanges.begin(), row.frequencyChanges.end(),
                  [](const FrequencyMark& a, const FrequencyMark& b) { return a.tNs < b.tNs; });
        std::sort(row.acquisitions.begin(), row.acquisitions.end(),
                  [](const AcquireSpan& a, const AcquireSpan& b) { return a.t0Ns < b.t0Ns; });
        model.rows.push_back(std::move(row));
    }
    std::stable_sort(model.rows.begin(), model.rows.end(), [](const PulseRow& a, const PulseRow& b) {
        const int ka = kindOrder(a.channel.kind), kb = kindOrder(b.channel.kind);
        if (ka != kb) return ka < kb;
        if (a.channel.a != b.channel.a) return a.channel.a < b.channel.a;
        return a.channel.b < b.channel.b;
    });
    return model;
}

std::optional<PulseReadout> readoutAt(const PulseModel& model, std::size_t row, double tNs) {
    if (row >= model.rows.size()) return std::nullopt;
    const PulseRow& r = model.rows[row];
    PulseReadout out;
    out.tNs = tNs;
    out.frequencyHz = r.frequencyHz;
    for (const FrequencyMark& f : r.frequencyChanges)
        if (f.tNs <= tNs) out.frequencyHz = f.frequencyHz;
    for (const FrameMark& p : r.phaseJumps)
        if (p.tNs <= tNs) out.framePhase = p.phase;
    for (const AcquireSpan& a : r.acquisitions) out.inAcquisition |= tNs >= a.t0Ns && tNs <= a.t1Ns;
    const std::vector<double>& ts = r.trace.tNs;
    if (!ts.empty() && tNs >= ts.front() - 0.5 * model.dtNs && tNs <= ts.back() + 0.5 * model.dtNs) {
        const auto it = std::lower_bound(ts.begin(), ts.end(), tNs);
        std::size_t k = static_cast<std::size_t>(it - ts.begin());
        if (k >= ts.size()) k = ts.size() - 1;
        if (k > 0 && tNs - ts[k - 1] < ts[k] - tNs) --k;
        out.re = r.trace.re[k];
        out.im = r.trace.im[k];
    }
    return out;
}

} // namespace qlab::viz::layout
