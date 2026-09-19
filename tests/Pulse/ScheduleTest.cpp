// Spec 10 §1, §4, §5, §11 — the time grid, schedule composition, and the verifier's negative
// corpus: misaligned start, short pulse, overlapping plays, unknown channel, dead phase.
#include "PulseTestSupport.hpp"

#include <catch2/catch_approx.hpp>

using namespace qlab;
using namespace qlab::pulse;
using pulsetest::library;

namespace {
constexpr std::int64_t kDt = 222, kGrain = 222 * 16;

Waveform pulseOfGranules(std::int64_t granules, double amp = 0.5) {
    const double T = static_cast<double>(granules * kGrain) * 1e-12;
    return Waveform::gaussianSquare(T, T / 8.0, T / 4.0, amp);
}

std::string verifyId(const Schedule& s, const hw::Device& dev,
                     std::vector<Warning>* warnings = nullptr) {
    auto r = s.verify(dev, warnings);
    return r ? std::string("ok") : r.error().diagnosticId;
}
} // namespace

TEST_CASE("durations quantise to whole granules of dt, never below the minimum pulse") {
    const Picoseconds dt{kDt};
    REQUIRE(quantiseSamples(32e-9, dt, 16) == 144); // 32 ns → 9 granules (31.968 ns)
    REQUIRE(quantise(32e-9, dt, 16).value == 9 * kGrain);
    REQUIRE(quantiseSamples(1e-12, dt, 16) == 16);     // a positive request keeps one granule
    REQUIRE(quantiseSamples(1e-12, dt, 16, 64) == 64); // raised to min_pulse_samples
    REQUIRE(quantiseSamples(0.0, dt, 16, 64) == 0);
    REQUIRE(quantiseSamples(-5e-9, dt, 16, 64) == 0);
    REQUIRE(quantiseSamples(440e-9 / 2.0, dt, 16) == 62 * 16); // 61.94 granules → 62
    for (double s : {1.8e-9, 17.3e-9, 200e-9, 700e-9,
                     1e-6}) { // ≥ half a granule: off by at most half a granule
        const auto q = quantise(s, dt, 16);
        REQUIRE(q.value % kGrain == 0);
        REQUIRE(std::abs(static_cast<double>(q.value) * 1e-12 - s) <= 0.5 * kGrain * 1e-12 + 1e-18);
    }
}

TEST_CASE("schedule composition: append ASAP per channel, barrier, block alignment") {
    Schedule s(Picoseconds{kDt});
    const auto d0 = ChannelId::drive(0), d1 = ChannelId::drive(1);
    s.append(Play{d0, pulseOfGranules(4), {}});
    s.append(Play{d0, pulseOfGranules(2), {}});
    s.append(Play{d1, pulseOfGranules(1), {}});
    REQUIRE(s.channelEnd(d0).value == 6 * kGrain);
    REQUIRE(s.channelEnd(d1).value == 1 * kGrain);
    REQUIRE(s.duration().value == 6 * kGrain);
    s.barrier({d0, d1});
    REQUIRE(s.channelEnd(d1).value == 6 * kGrain);
    s.append(FrameOp{d1, {}, FrameOp::Op::ShiftPhase, 0.4});
    REQUIRE(instructionStart(s.instructions().back()).value == 6 * kGrain);

    Schedule block(Picoseconds{kDt});
    block.append(Play{d0, pulseOfGranules(4), {}});
    block.append(Play{d1, pulseOfGranules(1), {}});
    Schedule left = s, right = s, seq = s;
    left.appendBlock(block, AlignMode::Left);
    right.appendBlock(block, AlignMode::Right);
    seq.appendBlock(block, AlignMode::Sequential);
    REQUIRE(left.channelEnd(d1).value == 7 * kGrain);   // ASAP
    REQUIRE(right.channelEnd(d1).value == 10 * kGrain); // ALAP: d1 pushed to the block's end
    REQUIRE(right.channelEnd(d0).value == 10 * kGrain);
    REQUIRE(seq.duration().value == 10 * kGrain);
    for (auto const* x : {&left, &right, &seq})
        REQUIRE(x->verifyTiming(16, 16).has_value());
    REQUIRE(left.verifyTiming(16, 64).error().diagnosticId ==
            "E_PULSE_MIN"); // the 16-sample d1 pulses
    const Schedule sliced = right.slice(Picoseconds{6 * kGrain}, Picoseconds{10 * kGrain});
    REQUIRE(sliced.duration().value == 4 * kGrain);
}

TEST_CASE("verify rejects the negative corpus with the spec 10 identifiers") {
    const PulseLibrary& fixed = library("sc_fixed_5");
    const hw::Device& dev = fixed.device();
    const auto d0 = ChannelId::drive(0);

    Schedule good(Picoseconds{kDt});
    good.insert(Play{d0, pulseOfGranules(4), {}}, Picoseconds{0});
    good.insert(Play{d0, pulseOfGranules(4), {}}, Picoseconds{4 * kGrain});
    REQUIRE(verifyId(good, dev) == "ok");

    Schedule misaligned(Picoseconds{kDt});
    misaligned.insert(Play{d0, pulseOfGranules(4), {}}, Picoseconds{kGrain + kDt});
    REQUIRE(verifyId(misaligned, dev) == "E_PULSE_ALIGN");
    REQUIRE(misaligned.verify(dev).error().code == kErrAlign);

    Schedule offGridLength(Picoseconds{kDt});
    offGridLength.insert(
        Play{d0, Waveform::constant(10 * kDt * 1e-12 + 64 * kGrain * 1e-12, 0.2), {}},
        Picoseconds{0});
    REQUIRE(verifyId(offGridLength, dev) == "E_PULSE_ALIGN");

    Schedule shortPulse(Picoseconds{kDt});
    shortPulse.insert(Play{d0, Waveform::constant(3 * kGrain * 1e-12, 0.2), {}},
                      Picoseconds{0}); // 48 < 64 samples
    REQUIRE(verifyId(shortPulse, dev) == "E_PULSE_MIN");
    REQUIRE(shortPulse.verify(dev).error().code == kErrMin);

    Schedule overlap(Picoseconds{kDt});
    overlap.insert(Play{d0, pulseOfGranules(4), {}}, Picoseconds{0});
    overlap.insert(Play{d0, pulseOfGranules(4), {}}, Picoseconds{2 * kGrain});
    REQUIRE(verifyId(overlap, dev) == "E_PULSE_OVERLAP");
    REQUIRE(overlap.verify(dev).error().code == kErrOverlap);

    auto onChannel = [&](ChannelId ch, const hw::Device& d, std::int64_t dt = kDt) {
        Schedule s(Picoseconds{dt});
        s.insert(Play{ch, Waveform::constant(static_cast<double>(64 * dt) * 1e-12, 0.1), {}},
                 Picoseconds{0});
        return verifyId(s, d);
    };
    REQUIRE(onChannel(ChannelId::drive(4), dev) == "ok");
    REQUIRE(onChannel(ChannelId::drive(5), dev) == "E_NO_CHANNEL"); // no qubit 5
    REQUIRE(onChannel(ChannelId::control(0, 1), dev) == "ok");
    REQUIRE(onChannel(ChannelId::control(0, 2), dev) == "E_NO_CHANNEL"); // 0 and 2 are not coupled
    REQUIRE(onChannel(ChannelId::flux(0), dev) == "E_NO_CHANNEL");  // fixed-frequency: no flux line
    REQUIRE(onChannel(ChannelId::raman(0), dev) == "E_NO_CHANNEL"); // ion channel on a transmon
    REQUIRE(onChannel(ChannelId::bichromatic(0, 1), dev) == "E_NO_CHANNEL");
    const hw::Device& grid = library("sc_tunable_grid_54").device();
    REQUIRE(onChannel(ChannelId::flux(54), grid) == "ok");            // coupler flux line
    REQUIRE(onChannel(ChannelId::drive(54), grid) == "E_NO_CHANNEL"); // couplers have no drive line
    REQUIRE(onChannel(ChannelId::measure(54), grid) == "E_NO_CHANNEL");
    const hw::Device& ions = library("ion_chain_11").device();
    REQUIRE(onChannel(ChannelId::bichromatic(2, 9), ions, 1000) == "ok");
    REQUIRE(onChannel(ChannelId::drive(0), ions, 1000) == "E_NO_CHANNEL");
    REQUIRE(onChannel(ChannelId::raman(11), ions, 1000) == "E_NO_CHANNEL");

    Schedule wrongDt(Picoseconds{250});
    wrongDt.insert(Play{d0, Waveform::constant(64 * 250 * 1e-12 * 16, 0.1), {}}, Picoseconds{0});
    REQUIRE(verifyId(wrongDt, dev) == "E_PULSE_ALIGN");
}

TEST_CASE("a phase shift with no later pulse is legal but reported as W_DEAD_PHASE") {
    const hw::Device& dev = library("sc_fixed_5").device();
    const auto d0 = ChannelId::drive(0), d1 = ChannelId::drive(1);
    Schedule s(Picoseconds{kDt});
    s.insert(FrameOp{d0, {}, FrameOp::Op::ShiftPhase, 0.5}, Picoseconds{0});
    s.insert(Play{d0, pulseOfGranules(4), {}}, Picoseconds{0});
    s.insert(FrameOp{d1, {}, FrameOp::Op::ShiftPhase, -0.25}, Picoseconds{4 * kGrain});
    std::vector<Warning> warnings;
    REQUIRE(verifyId(s, dev, &warnings) == "ok");
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].id == "W_DEAD_PHASE");
    REQUIRE(warnings[0].message.find("d[1]") != std::string::npos);
}
