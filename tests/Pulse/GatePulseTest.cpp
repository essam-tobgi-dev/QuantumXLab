// Spec 10 §6.1–§6.8, §7 — the defcal templates as oracles: DRAG area theorem, echoed-CR layout,
// virtual Z, flux gates, measurement and active reset timing.
#include "PulseTestSupport.hpp"

#include <catch2/catch_approx.hpp>

#include <numbers>

using namespace qlab;
using namespace qlab::pulse;
using Catch::Approx;
using pulsetest::library;

namespace {
constexpr double kPi = std::numbers::pi;

std::vector<Play> playsOn(const Schedule& s, ChannelId ch) {
    std::vector<Play> out;
    for (auto const& i : s.instructions())
        if (auto* p = std::get_if<Play>(&i); p && p->ch == ch)
            out.push_back(*p);
    return out;
}
std::vector<FrameOp> frameOps(const Schedule& s) {
    std::vector<FrameOp> out;
    for (auto const& i : s.instructions())
        if (auto* f = std::get_if<FrameOp>(&i))
            out.push_back(*f);
    return out;
}
// Trapezoid ∫A·e dt of the played envelope (amplitude included), independent of Waveform::area().
// Error ≈ (h²/6)|e'(0)|/area ≈ 1e−9 relative at n = 20000 for the 32 ns DRAG pulses.
double trapezoidArea(const Waveform& w, int n = 20000) {
    const double h = w.duration / n;
    double acc = 0.5 * (w.sample(0.0).real() + w.sample(w.duration).real());
    for (int i = 1; i < n; ++i)
        acc += w.sample(i * h).real();
    return acc * h;
}
} // namespace

TEST_CASE("X90 DRAG pulse: area theorem gives pi/2 within 1e-6 (spec 10 §7)") {
    for (const char* id : {"sc_fixed_5", "sc_heavyhex_27", "sc_tunable_grid_54"}) {
        INFO(id);
        const PulseLibrary& lib = library(id);
        for (auto q : lib.device().dataQubits()) {
            auto sx = lib.scheduleFor("sx", {q});
            auto x = lib.scheduleFor("x", {q});
            REQUIRE(sx);
            REQUIRE(x);
            const auto sxPlays = playsOn(*sx, ChannelId::drive(q));
            const auto xPlays = playsOn(*x, ChannelId::drive(q));
            REQUIRE(sxPlays.size() == 1);
            REQUIRE(xPlays.size() == 1);
            const Waveform& wSx = sxPlays[0].wf;
            REQUIRE(wSx.kind == WaveformKind::Drag);
            // σ = T/4 of the calibrated gate time (§6.1); T itself sits on the nearest granule
            const double calT = lib.calibration().qubit(q)->duration1q.value.v;
            REQUIRE(wSx.sigma == Approx(calT / 4.0).epsilon(1e-12));
            REQUIRE(std::abs(wSx.duration - calT) <=
                    0.5 * lib.dt().value * lib.granularity() * 1e-12 + 1e-18);
            // κ_d from the independent π calibration point (x), then θ(sx) = κ_d·A·area(e).
            const double kappa =
                kPi / trapezoidArea(xPlays[0].wf); // sample() carries the amplitude A_x
            const double theta = physics::rotationAngle(kappa, wSx);
            REQUIRE(std::abs(theta - kPi / 2.0) < 1e-6);
            REQUIRE(std::abs(wSx.area().imag()) <
                    1e-20); // the DRAG quadrature integrates to zero (§11)
            auto coupling = lib.driveCoupling(q);
            REQUIRE(coupling);
            REQUIRE(*coupling == Approx(kappa).epsilon(1e-6));
            REQUIRE(physics::amplitudeForAngle(kPi / 2.0, *coupling, wSx) ==
                    Approx(wSx.amplitude).epsilon(1e-9));
        }
    }
}

TEST_CASE(
    "echoed CR: two CR halves of opposite sign, echo pulses on the control, calibrated duration") {
    for (const char* id : {"sc_fixed_5", "sc_heavyhex_27"}) {
        const PulseLibrary& lib = library(id);
        const std::int64_t grain = lib.dt().value * lib.granularity();
        for (auto const& e : lib.device().edges) {
            INFO(id << " edge " << e.a << "-" << e.b);
            const std::uint32_t c = e.a, t = e.b;
            auto p = lib.crParams(c, t);
            auto cx = lib.scheduleFor("cx", {c, t});
            auto ecr = lib.scheduleFor("ecr", {c, t});
            REQUIRE(p);
            REQUIRE(cx);
            REQUIRE(ecr);
            const auto cr = playsOn(*cx, ChannelId::control(c, t));
            const auto cancel = playsOn(*cx, ChannelId::drive(t));
            const auto echo = playsOn(*cx, ChannelId::drive(c));
            REQUIRE(cr.size() == 2);
            REQUIRE(cancel.size() == 3); // ± cancellation tone, then the sx correction
            REQUIRE(echo.size() == 2);
            // halves of opposite sign and equal shape, back to back around the first echo
            REQUIRE(cr[0].wf.kind == WaveformKind::GaussianSquare);
            REQUIRE(cr[0].wf.amplitude == -cr[1].wf.amplitude);
            REQUIRE(cr[0].wf.amplitude == p->ampCr);
            REQUIRE(cr[0].wf.duration == cr[1].wf.duration);
            REQUIRE(cancel[0].wf.amplitude == -cancel[1].wf.amplitude);
            REQUIRE(cancel[0].wf.phase == p->phaseCancel);
            REQUIRE(cancel[0].t0 == cr[0].t0);
            REQUIRE(cancel[1].t0 == cr[1].t0);
            auto xOnControl = lib.scheduleFor("x", {c});
            REQUIRE(echo[0].wf == playsOn(*xOnControl, ChannelId::drive(c))[0].wf);
            REQUIRE(echo[0].t0 == cr[0].t0 + cr[0].duration());
            REQUIRE(cr[1].t0 == echo[0].t0 + echo[0].duration());
            REQUIRE(echo[1].t0 == cr[1].t0 + cr[1].duration());
            // total = 2·T_CR + 2·T_x, within one granule of cal.edges.c-t.duration_ns (spec 10 §1
            // grid)
            const std::int64_t total = 2 * (p->crDuration.value + p->echoDuration.value);
            REQUIRE(cx->duration().value == total);
            REQUIRE(ecr->duration().value == total);
            REQUIRE(std::abs(static_cast<double>(total) * 1e-12 - p->calibratedDurationS) <=
                    grain * 1e-12);
            REQUIRE(total % grain == 0);
            // ecr is steps 1–4 only; cx adds the target sx beside the second echo and virtual Zs.
            REQUIRE(frameOps(*ecr).empty());
            REQUIRE(playsOn(*ecr, ChannelId::drive(t)).size() == 2);
            REQUIRE(cancel[2].t0 == echo[1].t0);
            REQUIRE(cancel[2].wf ==
                    playsOn(*lib.scheduleFor("sx", {t}), ChannelId::drive(t))[0].wf);
            double shiftControl = 0.0, shiftTarget = 0.0;
            for (auto const& f : frameOps(*cx)) {
                REQUIRE(f.op == FrameOp::Op::ShiftPhase);
                if (f.ch == ChannelId::drive(c))
                    shiftControl += f.value;
                if (f.ch == ChannelId::drive(t))
                    shiftTarget += f.value;
            }
            REQUIRE(shiftControl == Approx(kPi / 2.0)); // Rz_c(−π/2) = shift_phase(+π/2)
            REQUIRE(shiftTarget == Approx(-2.0 * kPi)); // Rz_t(π) sx Rz_t(π)
        }
    }
}

TEST_CASE("virtual Z is frame ops only, zero duration, on every frame attached to the qubit") {
    const PulseLibrary& lib = library("sc_fixed_5");
    const double theta = 0.83;
    auto rz1 = lib.scheduleFor("rz", {1}, {{"theta", theta}});
    REQUIRE(rz1);
    REQUIRE(rz1->duration().value == 0);
    const auto ops = frameOps(*rz1);
    REQUIRE(ops.size() == rz1->size());
    REQUIRE(ops.size() == 2); // d[1] and u[0,1], the CR channel targeting qubit 1 (spec 10 §3)
    for (auto const& f : ops) {
        REQUIRE(f.op == FrameOp::Op::ShiftPhase);
        REQUIRE(f.value == -theta);
        REQUIRE(f.t0.value == 0);
    }
    REQUIRE(ops[0].ch != ops[1].ch);
    std::vector<Warning> warnings;
    REQUIRE(rz1->verify(lib.device(), &warnings));
    REQUIRE(warnings.size() == 2); // isolated phase shifts have no subsequent pulse: W_DEAD_PHASE
    REQUIRE(warnings[0].id == "W_DEAD_PHASE");

    const PulseLibrary& ions = library("ion_chain_11");
    auto rzIon = ions.scheduleFor("rz", {4}, {{"theta", theta}});
    REQUIRE(rzIon);
    REQUIRE(rzIon->duration().value == 0);
    REQUIRE(frameOps(*rzIon).size() == 1);
    REQUIRE(frameOps(*rzIon)[0].ch == ChannelId::raman(4));
}

TEST_CASE("flux CZ and iSWAP play one calibrated pulse on the coupler flux line") {
    const PulseLibrary& lib = library("sc_tunable_grid_54");
    const auto& edge = lib.device().edges.front();
    const std::uint32_t coupler = *edge.coupler;
    auto cz = lib.scheduleFor("cz", {edge.a, edge.b});
    auto iswap = lib.scheduleFor("siswap", {edge.a, edge.b});
    REQUIRE(cz);
    REQUIRE(iswap);
    const auto czPlays = playsOn(*cz, ChannelId::flux(coupler));
    const auto swPlays = playsOn(*iswap, ChannelId::flux(coupler));
    REQUIRE(czPlays.size() == 1);
    REQUIRE(swPlays.size() == 1);
    REQUIRE(czPlays[0].wf.kind == WaveformKind::Slepian);
    REQUIRE(czPlays[0].duration() ==
            lib.quantise(lib.calibration().edge(edge.a, edge.b)->duration.value.v, true));
    REQUIRE(czPlays[0].wf.amplitude ==
            lib.find("cz", std::vector<std::uint32_t>{edge.a, edge.b})->extra["amp"].get<double>());
    REQUIRE(swPlays[0].wf.kind == WaveformKind::GaussianSquare);
    REQUIRE(swPlays[0].wf.rise == Approx(5e-9));
    const auto& extra = lib.calibration().edge(edge.a, edge.b)->extraGates.at("siswap");
    REQUIRE(swPlays[0].duration() == lib.quantise(extra.second.value.v, true));
    // a flux pulse on a fixed-frequency device has no line: E_NO_CHANNEL (spec 10 §4)
    Schedule stray(library("sc_fixed_5").dt());
    stray.insert(Play{ChannelId::flux(0), czPlays[0].wf, {}}, Picoseconds{0});
    auto bad = stray.verify(library("sc_fixed_5").device());
    REQUIRE_FALSE(bad.has_value());
    REQUIRE(bad.error().diagnosticId == "E_NO_CHANNEL");
}

TEST_CASE(
    "measurement acquires T_ro after the ring-up delay; active reset waits out the feed-forward") {
    const PulseLibrary& lib = library("sc_fixed_5");
    auto m = lib.scheduleFor("measure", {2});
    REQUIRE(m);
    const auto tone = playsOn(*m, ChannelId::measure(2));
    REQUIRE(tone.size() == 1);
    const Acquire* acq = nullptr;
    for (auto const& i : m->instructions())
        if (auto* a = std::get_if<Acquire>(&i))
            acq = a;
    REQUIRE(acq != nullptr);
    REQUIRE(acq->t0 ==
            tone[0].t0 + lib.quantise(100e-9)); // acquire_delay after the tone starts (§6.7)
    REQUIRE(acq->length == tone[0].duration());
    REQUIRE(acq->weights == "matched");

    auto r = lib.scheduleFor("reset", {2});
    REQUIRE(r);
    const auto x = playsOn(*r, ChannelId::drive(2));
    REQUIRE(x.size() == 1);
    REQUIRE(x[0].t0 == m->duration() + lib.quantise(200e-9)); // timing.feedforward_ns (§6.8)
    REQUIRE(x[0].wf == playsOn(*lib.scheduleFor("x", {2}), ChannelId::drive(2))[0].wf);

    const PulseLibrary& ions = library("ion_chain_11");
    auto pump = ions.scheduleFor("reset", {0});
    REQUIRE(pump);
    REQUIRE(pump->duration() == ions.quantise(10e-6, true)); // 10 µs optical pumping (§6.8)
    REQUIRE(playsOn(*pump, ChannelId::pump()).size() == 1);
}
