// Spec 10 §1, spec 12 §3, spec 25 §3 — schedule sampling: the device-rate record reproduces the
// envelopes exactly on whole granules; a 1 GS/s record stays within the zero-order-hold tolerance.
#include "PulseTestSupport.hpp"

#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <limits>
#include <numbers>

using namespace qlab;
using namespace qlab::pulse;
using Catch::Approx;
using pulsetest::library;

namespace {
constexpr double kPi = std::numbers::pi;

// max |d/dt (A·e(t))| of a waveform, by central differences on a fine grid (+1 % margin).
double maxSlope(const Waveform& w) {
    const int n = 20000;
    const double h = w.duration / n;
    double m = 0.0;
    for (int i = 1; i < n; ++i)
        m = std::max(m, std::abs(w.sample((i + 0.5) * h) - w.sample((i - 0.5) * h)) / h);
    return 1.01 * m;
}

Schedule cxOnFixed5() {
    auto s = library("sc_fixed_5").scheduleFor("cx", {0, 1});
    REQUIRE(s);
    return *s;
}
} // namespace

TEST_CASE("device-rate sampling reproduces every envelope on whole 16-sample granules") {
    const Schedule cx = cxOnFixed5();
    const double rate = 1e12 / 222.0;
    auto rec = sampleSchedule(cx, rate);
    REQUIRE(rec);
    REQUIRE(rec->dt.value == 222);
    REQUIRE(rec->length % 16 == 0);
    REQUIRE(static_cast<std::int64_t>(rec->length) * 222 >= cx.duration().value);
    REQUIRE(static_cast<std::int64_t>(rec->length) * 222 - cx.duration().value < 16 * 222);
    REQUIRE(rec->channels.size() == 3); // u[0,1], d[0], d[1]
    const FrameTimeline frames(cx);
    std::size_t plays = 0;
    for (auto const& instr : cx.instructions()) {
        auto* p = std::get_if<Play>(&instr);
        if (!p)
            continue;
        const ChannelSamples* cs = rec->find(p->ch);
        REQUIRE(cs != nullptr);
        REQUIRE(cs->frameFrequencyHz == library("sc_fixed_5").frameFor(p->ch)->frequencyHz);
        const std::size_t first = static_cast<std::size_t>(p->t0.value / 222);
        const std::size_t count = static_cast<std::size_t>(p->duration().value / 222);
        REQUIRE(first % 16 == 0);
        REQUIRE(count % 16 == 0);
        for (std::size_t k = 0; k < count; ++k) {
            const double tk = static_cast<double>((first + k) * 222) * 1e-12;
            const Complex want = p->wf.sample(static_cast<double>(k * 222) * 1e-12) *
                                 std::polar(1.0, frames.basebandPhase(p->ch, tk));
            REQUIRE(std::abs(cs->samples[first + k] - want) <= 1e-12);
        }
        ++plays;
    }
    REQUIRE(plays == 7);
    for (auto const& cs : rec->channels)
        for (auto const& span : cs.plays) {
            REQUIRE(span.start % 16 == 0);
            REQUIRE(span.count % 16 == 0);
        }
    // Rz_t(π) precedes the target correction: the sx samples are the plain sx samples times
    // e^{−iπ}.
    const ChannelSamples* target = rec->find(ChannelId::drive(1));
    const auto& sxSpan = target->plays.back();
    auto sxAlone = sampleSchedule(*library("sc_fixed_5").scheduleFor("sx", {1}), rate);
    REQUIRE(sxAlone);
    for (std::size_t k = 0; k < sxSpan.count; ++k)
        REQUIRE(std::abs(target->samples[sxSpan.start + k] + sxAlone->channels[0].samples[k]) <=
                1e-12);
}

TEST_CASE(
    "1 GS/s sampling: dt quantisation, granularity, and zero-order-hold interpolation tolerance") {
    const PulseLibrary& lib = library("sc_fixed_5");
    for (const char* gate : {"sx", "cx", "measure"}) {
        INFO(gate);
        const std::vector<std::uint32_t> qubits = std::string(gate) == "cx"
                                                      ? std::vector<std::uint32_t>{0, 1}
                                                      : std::vector<std::uint32_t>{0};
        auto s = lib.scheduleFor(gate, qubits);
        REQUIRE(s);
        auto dev = sampleSchedule(*s, 1e12 / 222.0);
        auto gs = sampleSchedule(*s, 1e9);
        REQUIRE(dev);
        REQUIRE(gs);
        REQUIRE(gs->dt.value == 1000);
        REQUIRE(gs->length % 16 == 0);
        REQUIRE(static_cast<std::int64_t>(gs->length) * 1000 >= s->duration().value);
        REQUIRE(gs->channels.size() == dev->channels.size());
        for (auto const& instr : s->instructions()) {
            auto* p = std::get_if<Play>(&instr);
            if (!p)
                continue;
            // |s_1GS(t_k) − s_dev(t_j)| ≤ max|ṡ|·222 ps, t_j = ⌊t_k/222 ps⌋·222 ps: frame ops and
            // play edges sit on 16·222 ps granules, so no jump falls between t_j and t_k.
            const double tolerance = maxSlope(p->wf) * 222e-12;
            const ChannelSamples* a = gs->find(p->ch);
            const ChannelSamples* b = dev->find(p->ch);
            REQUIRE(a != nullptr);
            REQUIRE(b != nullptr);
            const std::size_t first = static_cast<std::size_t>((p->t0.value + 999) / 1000);
            const std::size_t last =
                static_cast<std::size_t>((p->t0.value + p->duration().value + 999) / 1000);
            bool withinPlaySpans =
                std::any_of(a->plays.begin(), a->plays.end(), [&](const SampleSpan& sp) {
                    return sp.start == first && sp.count == last - first;
                });
            REQUIRE(withinPlaySpans);
            double worst = 0.0;
            for (std::size_t k = first; k < last; ++k) {
                const std::size_t j = (k * 1000) / 222;
                worst = std::max(worst, std::abs(a->samples[k] - b->samples[j]));
            }
            INFO("worst deviation " << worst << " tolerance " << tolerance);
            REQUIRE(worst <= tolerance);
        }
    }
    // acquisition windows move to the new grid: [⌈t0/dt⌉, ⌈(t0 + T)/dt⌉)
    auto m = lib.scheduleFor("measure", {1});
    auto rec = sampleSchedule(*m, 1e9);
    REQUIRE(rec->acquisitions.size() == 1);
    REQUIRE(rec->acquisitions[0].span.start == 100); // 99 456 ps → sample 100
    REQUIRE(rec->acquisitions[0].span.count == 700); // ends at 799 200 ps → sample 800
    REQUIRE(rec->acquisitions[0].weights == "matched");
}

TEST_CASE("frame phase and frequency instructions shape the sampled baseband") {
    const PulseLibrary& lib = library("sc_fixed_5");
    const double theta = 1.1, shiftHz = 3e6;
    const Schedule sx = *lib.scheduleFor("sx", {0});
    Schedule withZ = *lib.scheduleFor("rz", {0}, {{"theta", theta}});
    withZ.appendBlock(sx, AlignMode::Sequential);
    Schedule withShift(lib.dt());
    for (auto const& f : sx.frames())
        withShift.addFrame(f);
    withShift.insert(FrameOp{ChannelId::drive(0), {}, FrameOp::Op::ShiftFrequency, shiftHz},
                     Picoseconds{0});
    withShift.insert(std::get<Play>(sx.instructions().front()), Picoseconds{0});

    const double rate = 1e12 / 222.0;
    auto plain = sampleSchedule(sx, rate);
    auto z = sampleSchedule(withZ, rate);
    auto shifted = sampleSchedule(withShift, rate);
    REQUIRE(plain);
    REQUIRE(z);
    REQUIRE(shifted);
    const auto& p = plain->channels[0].samples;
    for (std::size_t k = 0; k < p.size(); ++k) {
        // virtual Rz(θ) = shift_phase(−θ): every later sample carries e^{−iθ}
        REQUIRE(std::abs(z->find(ChannelId::drive(0))->samples[k] -
                         p[k] * std::polar(1.0, -theta)) <= 1e-15);
        // a frame detuned by +δf rotates as e^{−i2πδf t} against the declared frame
        const double tk = static_cast<double>(k * 222) * 1e-12;
        // (1e-12: f_declared + δf − f_declared recovers δf only to the ulp of a 5 GHz double)
        REQUIRE(std::abs(shifted->channels[0].samples[k] -
                         p[k] * std::polar(1.0, -2.0 * kPi * shiftHz * tk)) <= 1e-12);
    }
    REQUIRE(FrameTimeline(withZ).phaseOps(ChannelId::drive(0), 1e-6) == -theta);
    REQUIRE(FrameTimeline(withShift).frequencyHz(ChannelId::drive(0), 0.0) ==
            Approx(lib.frameFor(ChannelId::drive(0))->frequencyHz + shiftHz).epsilon(1e-15));
}

TEST_CASE("invalid sample rates and granularities are rejected") {
    const Schedule cx = cxOnFixed5();
    for (double rate : {0.0, -1e9, std::nan(""), std::numeric_limits<double>::infinity(), 1e13}) {
        INFO(rate);
        auto r = sampleSchedule(cx, rate);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == kErrSampling);
    }
    REQUIRE(sampleSchedule(cx, 1e9, 0).error().code == kErrSampling);
    auto empty = sampleSchedule(Schedule(Picoseconds{222}), 1e9);
    REQUIRE(empty);
    REQUIRE(empty->length == 0);
    REQUIRE(empty->channels.empty());
    REQUIRE(sampleSchedule(cx, 1e9, 1)->length ==
            static_cast<std::size_t>((cx.duration().value + 999) / 1000));
}
