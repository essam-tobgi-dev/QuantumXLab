// Spec 14 §6 — pulse lowering: a Bell circuit on sc_fixed_5 yields a schedule that passes
// `pulse::Schedule::verify`; gate-level and pulse-level timing agree; rz is a frame phase shift;
// cx is the echoed cross-resonance sequence; program defcals override the device table; QL4080,
// QL4040, conditional arms, port names; ions and tunable couplers.
#include "CompilerTestUtil.hpp"
#include "Pulse/Pulse.hpp"
#include <algorithm>
#include <cmath>
#include <set>

using namespace ctest;

namespace {
compiler::CompiledProgram compiled(const std::string& src, const std::string& id,
                                   compiler::CompileOptions o = {}) {
    o.pulseLevel = true;
    const auto prog = parse(src);
    auto out = compiler::compile(prog, device(id).device, device(id).calibration, o);
    INFO(src << "\n" << (out ? std::string() : compiler::formatError(out.error())));
    REQUIRE(out.has_value());
    REQUIRE(out->pulses.has_value());
    return std::move(*out);
}
std::vector<const pulse::Play*> playsOn(const pulse::Schedule& s, pulse::ChannelId ch) {
    std::vector<const pulse::Play*> v;
    for (const auto& i : s.instructions())
        if (const auto* p = std::get_if<pulse::Play>(&i); p && p->ch == ch)
            v.push_back(p);
    return v;
}
} // namespace

TEST_CASE("a Bell circuit on sc_fixed_5 lowers to a schedule that passes pulse::verify") {
    const auto out = compiled(
        program("qubit[2] q; bit[2] c; h q[0]; cx q[0], q[1]; c = measure q;"), "sc_fixed_5");
    const pulse::Schedule& s = out.pulses->schedule;
    std::vector<pulse::Warning> warnings;
    auto verified = s.verify(device("sc_fixed_5").device, &warnings);
    INFO((verified ? std::string() : verified.error().format()));
    REQUIRE(verified.has_value());
    CHECK(s.dt().get() == 222);

    // The cx sits on a native-direction edge: echoed CR = two CR tones on u[c,t] around two echo
    // pulses on d[c] (spec 14 §6), plus the single-qubit corrections.
    const ir::Gate* cx = nullptr;
    for (const ir::Gate* g : gatesOf(out.circuit))
        if (g->name == "cx")
            cx = g;
    REQUIRE(cx != nullptr);
    const std::uint32_t c = cx->targets[0].index, t = cx->targets[1].index;
    REQUIRE(device("sc_fixed_5").device.nativeDirection(c, t));
    const auto cr = playsOn(s, pulse::ChannelId::control(c, t));
    REQUIRE(cr.size() == 2);
    CHECK(cr[0]->wf.kind == pulse::WaveformKind::GaussianSquare);
    CHECK(cr[0]->wf.amplitude == -cr[1]->wf.amplitude);        // CR(+) … CR(−)
    CHECK(playsOn(s, pulse::ChannelId::drive(c)).size() >= 2); // the two echo pulses
    // Readout: a tone on m[q] and an acquisition window on a[q] for both measured qubits.
    std::set<pulse::ChannelId> channels = s.channels();
    for (std::uint32_t q : {c, t}) {
        CHECK(channels.contains(pulse::ChannelId::measure(q)));
        CHECK(channels.contains(pulse::ChannelId::acquire(q)));
    }
    // Gate-level and pulse-level timing agree: every node's window is its scheduled slot, no play
    // starts before its node or ends after it.
    REQUIRE(out.pulses->windows.size() == out.timing.nodes.size());
    for (const auto& w : out.pulses->windows) {
        const auto& slot = out.timing.nodes[w.node];
        CHECK(w.start == slot.start);
        CHECK(w.end == slot.end());
        CHECK_FALSE(w.conditional);
    }
    for (const auto& i : s.instructions()) { // every pulse lies inside the slot of some node
        const auto t0 = pulse::instructionStart(i), t1 = t0 + pulse::instructionDuration(i);
        const bool inside =
            std::any_of(out.pulses->windows.begin(), out.pulses->windows.end(),
                        [&](const auto& w) { return w.start <= t0 && t1 <= w.end; });
        CHECK(inside);
    }
    CHECK(s.duration() == out.timing.duration);
    CHECK(out.metrics.estimatedDuration == out.timing.duration);
    // The calibrated cx length and the pulse block agree within one granule (spec 10 §6.3).
    const double calibrated = device("sc_fixed_5").calibration.edge(c, t)->duration.value.si();
    CHECK(std::abs(static_cast<double>(cx->duration->get()) * 1e-12 - calibrated) <= 222e-12 * 16);
    REQUIRE(out.equivalence.has_value());
    CHECK(out.equivalence->equivalent);
}

TEST_CASE("rz lowers to shift_phase(−θ) on the drive frame: the accumulated virtual-Z phase") {
    const auto out = compiled(program("pragma qlab.layout physical\npragma qlab.optimize "
                                      "0\nrz(0.3) $1; sx $1; rz(0.5) $1; sx $1;"),
                              "sc_fixed_5");
    const pulse::FrameTimeline frames(out.pulses->schedule);
    const auto d1 = pulse::ChannelId::drive(1);
    const double sx = pulse::secondsOf(out.timing.nodes[1].duration);
    CHECK(std::abs(frames.phaseOps(d1, 0.5 * sx) + 0.3) < 1e-12); // first pulse: −0.3
    CHECK(std::abs(frames.phaseOps(d1, 1.5 * sx) + 0.8) < 1e-12); // second pulse: −(0.3 + 0.5)
    CHECK(playsOn(out.pulses->schedule, d1).size() == 2);
    CHECK(out.timing.nodes[0].duration.get() == 0); // rz costs no time
    // Every CR frame that targets qubit 1 follows (spec 10 §3): u[0,1] on this device.
    CHECK(std::abs(frames.phaseOps(pulse::ChannelId::control(0, 1), 1.5 * sx) + 0.8) < 1e-12);
}

TEST_CASE("a program defcal overrides the device pulse and sets the gate-level duration") {
    const auto out = compiled(R"(OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.layout physical
cal { extern port d0; frame df0 = newframe(d0, 4.8e9, 0.0); waveform soft = gaussian(0.2, 48ns, 12ns); }
defcal x $0 { play(df0, drag(0.48, 40ns, 10ns, 0.18)); }
defcal xb(angle b) $0 { play(df0, drag(0.5, 24ns, 6ns, b)); shift_phase(df0, b); play(df0, soft); }
bit c;
x $0; xb(0.25) $0; x $1;
c = measure $0;
)",
                              "sc_fixed_5");
    const auto plays = playsOn(out.pulses->schedule, pulse::ChannelId::drive(0));
    REQUIRE(plays.size() == 3);
    const std::int64_t granule = 222 * 16;
    CHECK(plays[0]->wf.kind == pulse::WaveformKind::Drag);
    CHECK(plays[0]->wf.amplitude == 0.48);
    CHECK(std::abs(plays[0]->wf.beta - 0.18e-9) < 1e-24); // β in nanoseconds
    CHECK(plays[0]->duration().get() == 11 * granule);    // 40 ns → 39.072 ns
    CHECK(plays[1]->wf.amplitude == 0.5);
    CHECK(std::abs(plays[1]->wf.beta - 0.25e-9) < 1e-24); // the gate's argument is bound to `b`
    CHECK(plays[2]->wf.kind ==
          pulse::WaveformKind::Gaussian); // the named waveform of the cal block
    // The user pulse is the duration the scheduler used; x $1 keeps the device's 32 ns pulse.
    CHECK(out.timing.nodes[0].duration.get() == 11 * granule);
    CHECK(out.timing.nodes[2].duration.get() == 9 * granule);
    CHECK(out.timing.nodes[1].duration.get() ==
          7 * granule + 14 * granule); // 24 ns + 48 ns on the grid
    // The program's frame precedes the device's for d[0]; the shift_phase of xb is recorded on it.
    const pulse::FrameTimeline frames(out.pulses->schedule);
    CHECK(frames.declaredFrequencyHz(pulse::ChannelId::drive(0)) == 4.8e9);
    CHECK(std::abs(
              frames.phaseOps(pulse::ChannelId::drive(0), pulse::secondsOf(out.timing.duration)) -
              0.25) < 1e-12);
    CHECK(out.equivalence->method == compiler::EquivalenceMethod::Skipped); // xb has no matrix
}

TEST_CASE("the shipped pulse examples lower and verify") {
    for (const char* name : {"defcal_x_custom", "drag_beta_scan", "cross_resonance_echo"}) {
        const auto path =
            core::assetDir() / "Programs" / "Examples" / "Pulse" / (std::string(name) + ".qasm");
        auto text = core::readTextFile(path);
        REQUIRE(text.has_value());
        const auto out = compiled(*text, "sc_fixed_5");
        INFO(name);
        CHECK(out.pulses->schedule.verify(device("sc_fixed_5").device).has_value());
        CHECK(out.pulses->schedule.size() > 0);
    }
    const auto echo = compiled(*core::readTextFile(core::assetDir() / "Programs" / "Examples" /
                                                   "Pulse" / "cross_resonance_echo.qasm"),
                               "sc_fixed_5");
    const auto cr = playsOn(echo.pulses->schedule, pulse::ChannelId::control(0, 1));
    REQUIRE(cr.size() == 2);           // port u01 is u[0,1]
    CHECK(cr[0]->wf.amplitude == 0.3); // the input's default, bound by Build
    CHECK(std::abs(cr[0]->wf.rise - (cr[0]->wf.duration - 160e-9) / 2) < 1e-15);
}

TEST_CASE("QL4080 without any calibration, QL4040 above the Lindblad cap, conditional arms") {
    const auto& d = device("sc_fixed_5");
    compiler::CompileOptions o;
    o.pulseLevel = true;
    // A defcal-only gate called on qubits its defcal does not cover.
    auto prog = parse(R"(OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.layout physical
cal { extern port d0; frame df0 = newframe(d0, 4.8e9, 0.0); }
defcal kick $0 { play(df0, constant(0.1, 32ns)); }
kick $1;
)");
    auto missing = compiler::compile(prog, d.device, d.calibration, o);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().diagnosticId == "QL4080");
    CHECK(missing.error().message == "no calibration for 'kick' on $1");
    CHECK(missing.error().span->line == 7);

    // Six transmons exceed the pulse-level cap of five (spec 15 §2) … on a 27-qubit device.
    const auto& big = device("sc_heavyhex_27");
    auto six = parse(program("qubit[6] q; h q;"));
    auto capped = compiler::compile(six, big.device, big.calibration, o);
    REQUIRE_FALSE(capped.has_value());
    CHECK(capped.error().diagnosticId == "QL4040");
    o.enforcePulseQubitCap = false; // the pulse viewer may look at more
    CHECK(compiler::compile(six, big.device, big.calibration, o).has_value());

    // `if` without `else`: the arm is placed after the feedback latency and marked conditional.
    const auto ff = compiled(
        program("pragma qlab.layout physical\nbit c; x $0; c = measure $0; if (c == 1) { x $0; }"),
        "sc_fixed_5");
    std::size_t conditional = 0;
    for (const auto& w : ff.pulses->windows) {
        if (!w.conditional) {
            CHECK(w.parent == compiler::PulseWindow::kTopLevel);
            continue;
        }
        ++conditional;
        CHECK(w.condition.find("==") != std::string::npos);
        CHECK(w.parent == 2); // the Branch is the third top-level node
        // The arm starts one feedback latency (200 ns on the grid) after the branch slot begins.
        CHECK(w.start.get() ==
              ff.timing.nodes[2].start.get() + pulse::quantise(200e-9, Picoseconds{222}, 16).get());
        CHECK(w.start >= ff.timing.nodes[1].end());
    }
    CHECK(conditional == 1);
    auto withElse = parse(program("pragma qlab.layout physical\nbit c; c = measure $0; if (c == 1) "
                                  "{ x $0; } else { sx $0; }"));
    o.enforcePulseQubitCap = true;
    auto unsupported = compiler::compile(withElse, d.device, d.calibration, o);
    REQUIRE_FALSE(unsupported.has_value());
    CHECK(unsupported.error().code == compiler::err::Unsupported);
}

TEST_CASE("port names map to channels; ions and tunable couplers lower and verify") {
    using pulse::ChannelId;
    CHECK(*compiler::channelOfPort("d0") == ChannelId::drive(0));
    CHECK(*compiler::channelOfPort("d_12") == ChannelId::drive(12));
    CHECK(*compiler::channelOfPort("u01") == ChannelId::control(0, 1));
    CHECK(*compiler::channelOfPort("u3_14") == ChannelId::control(3, 14));
    CHECK(*compiler::channelOfPort("m4") == ChannelId::measure(4));
    CHECK(*compiler::channelOfPort("acq4") == ChannelId::acquire(4));
    CHECK(*compiler::channelOfPort("f60") == ChannelId::flux(60));
    CHECK(*compiler::channelOfPort("ms0_2") == ChannelId::bichromatic(0, 2));
    CHECK_FALSE(compiler::channelOfPort("u123").has_value()); // ambiguous without a separator
    CHECK_FALSE(compiler::channelOfPort("xyz").has_value());

    const std::string bell = program("qubit[2] q; bit[2] c; h q[0]; cx q[0], q[1]; c = measure q;");
    const auto ion = compiled(bell, "ion_chain_11");
    CHECK(countGates(ion.circuit, "ms") == 1);
    bool bichromatic = false;
    for (auto ch : ion.pulses->schedule.channels())
        bichromatic = bichromatic || ch.kind == pulse::ChannelKind::Bichromatic;
    CHECK(bichromatic);
    const auto grid = compiled(bell, "sc_tunable_grid_54");
    CHECK(countGates(grid.circuit, "cz") == 1);
    bool flux = false;
    for (auto ch : grid.pulses->schedule.channels())
        flux = flux || ch.kind == pulse::ChannelKind::Flux;
    CHECK(flux);
    CHECK(grid.pulses->schedule.verify(device("sc_tunable_grid_54").device).has_value());
}
