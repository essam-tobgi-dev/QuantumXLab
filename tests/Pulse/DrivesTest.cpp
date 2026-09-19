// Spec 10 §3, §7, §8, §11 — schedules as Lindblad drive envelopes: Rabi scale, the two-level area
// theorem, exact virtual Z, the CR detuning in the control frame, the lab frame, and events.
#include "PulseTestSupport.hpp"

#include "QSim/SystemModel.hpp" // the struct the envelopes are handed to (spec 07 §5); header-only here

#include <catch2/catch_approx.hpp>

#include <numbers>

using namespace qlab;
using namespace qlab::pulse;
using Catch::Approx;
using pulsetest::library;
using pulsetest::processFidelity;
using pulsetest::propagate;

namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

const DriveEnvelope& only(const SystemDrives& d, std::string_view channel) {
    const DriveEnvelope* e = d.find(channel);
    REQUIRE(e != nullptr);
    return *e;
}
} // namespace

TEST_CASE("sx drive: Ω = κ_d·A·e(t) and the two-level area theorem yields Rx(pi/2)") {
    const PulseLibrary& lib = library("sc_fixed_5");
    const Schedule sx = *lib.scheduleFor("sx", {0});
    auto drives = toSystemDrives(sx, lib);
    REQUIRE(drives);
    REQUIRE(drives->drives.size() == 1);
    REQUIRE(drives->events.empty());
    const DriveEnvelope& d = only(*drives, "d[0]");
    REQUIRE(d.kind == DriveKind::Rabi);
    REQUIRE(d.site == 0);
    REQUIRE(d.scale == *lib.driveCoupling(0));
    REQUIRE(d.referenceFrequencyHz ==
            d.channelFrequencyHz); // resonant: no rotation in the qubit frame
    const Play& play = std::get<Play>(sx.instructions().front());
    REQUIRE(d.held.size() == static_cast<std::size_t>(play.duration().value / 222));
    for (std::size_t k = 0; k < d.held.size(); k += 17)
        REQUIRE(std::abs(d.held[k] - d.scale * play.wf.sample(static_cast<double>(k) * 222e-12)) <=
                1e-15 * d.scale);
    REQUIRE(d.at(5e-9) == d.held[static_cast<std::size_t>(5e-9 / 222e-12)]);
    REQUIRE(d.envelope()(5e-9) == d.at(5e-9));
    REQUIRE(d.at(-1e-12) == Complex{});
    REQUIRE(d.at(1.0) == Complex{});

    // Σ Re Ω_k dt = π/2 up to the zero-order-hold (left Riemann) error (h²/6)·|Ω'(0)| of a lifted
    // Gaussian.
    double sum = 0.0;
    for (auto const& w : d.held)
        sum += w.real() * d.dtS;
    const double slope0 =
        d.scale * play.wf.amplitude * gaussianShapeDerivative(0.0, play.wf.duration, play.wf.sigma);
    REQUIRE(std::abs(sum - kPi / 2.0) <= 2.0 * d.dtS * d.dtS / 6.0 * std::abs(slope0));

    // Spec 10 §11: the area theorem on the two-level RWA model gives Rx(π/2) to 1e−9 (in-phase
    // envelope; the DRAG quadrature is a three-level correction).
    Schedule plain(lib.dt());
    for (auto const& f : sx.frames())
        plain.addFrame(f);
    Play noDrag = play;
    noDrag.wf.beta = 0.0;
    plain.insert(noDrag, Picoseconds{0});
    const auto u = propagate(only(*toSystemDrives(plain, lib), "d[0]").held, d.dtS);
    REQUIRE(processFidelity(u, pulsetest::rx(kPi / 2.0)) > 1.0 - 1e-9);
}

TEST_CASE(
    "virtual Z: shift_phase(-theta) between two sx pulses is Rz(theta) exactly (spec 10 §3)") {
    const PulseLibrary& lib = library("sc_fixed_5");
    const Schedule sx = *lib.scheduleFor("sx", {0});
    const auto uSx = propagate(only(*toSystemDrives(sx, lib), "d[0]").held, 222e-12);
    for (double theta : {0.3, 1.2, kPi, -2.0}) {
        INFO(theta);
        Schedule seq = sx;
        seq.appendBlock(*lib.scheduleFor("rz", {0}, {{"theta", theta}}), AlignMode::Sequential);
        seq.appendBlock(sx, AlignMode::Sequential);
        auto drives = toSystemDrives(seq, lib);
        REQUIRE(drives);
        const auto u = propagate(only(*drives, "d[0]").held, 222e-12);
        // The frame change leaves a trailing Rz(−θ) that commutes into the Z measurement (T07 §5):
        // Rz(θ)·U = U_sx·Rz(θ)·U_sx.
        const auto want = pulsetest::mul(uSx, pulsetest::mul(pulsetest::rz(theta), uSx));
        REQUIRE(processFidelity(pulsetest::mul(pulsetest::rz(theta), u), want) > 1.0 - 1e-12);
        REQUIRE(std::norm(u[2]) == Approx(std::norm(want[2])).margin(1e-12)); // P(1) from |0⟩
        if (std::abs(std::sin(theta)) > 0.5) { // the opposite sign convention would be detected
            const auto wrong = pulsetest::mul(uSx, pulsetest::mul(pulsetest::rz(-theta), uSx));
            REQUIRE(processFidelity(pulsetest::mul(pulsetest::rz(theta), u), wrong) < 0.99);
        }
    }
}

TEST_CASE(
    "CR tone: rotates at f_t - f_c in the control frame; the lab frame carries the full carrier") {
    const PulseLibrary& lib = library("sc_fixed_5");
    const Schedule cx = *lib.scheduleFor("cx", {0, 1});
    auto rot = toSystemDrives(cx, lib);
    auto lab = toSystemDrives(cx, lib, DriveOptions{false, std::nullopt});
    REQUIRE(rot);
    REQUIRE(lab);
    REQUIRE(rot->drives.size() == 3);
    const DriveEnvelope& cr = only(*rot, "u[0,1]");
    const double f0 = lib.calibration().qubit(0)->f01.value.v,
                 f1 = lib.calibration().qubit(1)->f01.value.v;
    REQUIRE(cr.site == 0);
    REQUIRE(cr.target == 1u);
    REQUIRE(cr.scale == *lib.driveCoupling(0)); // same physical line as d[0] (spec 10 §4)
    REQUIRE(cr.channelFrequencyHz == Approx(f1).epsilon(1e-15));
    REQUIRE(cr.referenceFrequencyHz == Approx(f0).epsilon(1e-15));
    const DriveEnvelope& crLab = only(*lab, "u[0,1]");
    REQUIRE(crLab.referenceFrequencyHz == 0.0);
    for (double t : {1.0e-9, 37.3e-9, 150.0e-9, 300.1e-9}) {
        const Complex h = cr.held[static_cast<std::size_t>(t / cr.dtS)];
        REQUIRE(std::abs(cr.at(t) - h * std::polar(1.0, -kTwoPi * (f1 - f0) * t)) <=
                1e-12 * std::abs(h));
        const double labValue = 2.0 * (h * std::polar(1.0, -kTwoPi * f1 * t)).real();
        REQUIRE(crLab.at(t).imag() == 0.0);
        REQUIRE(crLab.at(t).real() == Approx(labValue).margin(1e-9 * std::abs(h)));
        // RWA: demodulating the lab signal over one carrier period inside the hold interval
        // recovers Ω.
        const double t0 = std::floor(t / cr.dtS) * cr.dtS, period = 1.0 / f1;
        REQUIRE(period < cr.dtS);
        Complex avg{};
        const int n = 4000;
        for (int i = 0; i < n; ++i) {
            const double ti = t0 + (i + 0.5) * period / n;
            avg += crLab.at(ti) * std::polar(1.0, kTwoPi * f1 * ti) / static_cast<double>(n);
        }
        REQUIRE(std::abs(avg - h) <= 1e-6 * std::abs(h));
    }
    // the cancellation tone and the corrections sit in the target's own frame: no rotation
    const DriveEnvelope& target = only(*rot, "d[1]");
    REQUIRE(target.referenceFrequencyHz == target.channelFrequencyHz);
    const DriveOptions shared{true, 4.9e9};
    REQUIRE(only(*toSystemDrives(cx, lib, shared), "d[1]").referenceFrequencyHz == 4.9e9);
}

TEST_CASE("readout, acquisition, detection and pumping become schedule events") {
    const PulseLibrary& lib = library("sc_fixed_5");
    auto m = toSystemDrives(*lib.scheduleFor("measure", {2}), lib);
    REQUIRE(m);
    REQUIRE(m->drives.empty());
    REQUIRE(m->events.size() == 2);
    REQUIRE(m->events[0].kind == ScheduleEvent::Kind::ReadoutTone);
    REQUIRE(m->events[0].qubit == 2u);
    REQUIRE(m->events[1].kind == ScheduleEvent::Kind::Acquire);
    REQUIRE(m->events[1].t0S == Approx(99.456e-9).epsilon(1e-12));
    REQUIRE(m->events[1].durationS == Approx(m->events[0].durationS).epsilon(1e-15));
    REQUIRE(m->events[1].weights == "matched");

    const PulseLibrary& ions = library("ion_chain_11");
    auto det = toSystemDrives(*ions.scheduleFor("measure", {3}), ions);
    REQUIRE(det);
    REQUIRE(det->events.size() == 2);
    REQUIRE(det->events[0].kind == ScheduleEvent::Kind::Detect);
    REQUIRE(det->events[1].kind == ScheduleEvent::Kind::Acquire);
    REQUIRE(det->events[1].acquire == AcquireKind::PhotonCount);
    auto pump = toSystemDrives(*ions.scheduleFor("reset", {3}), ions);
    REQUIRE(pump->events.size() == 1);
    REQUIRE(pump->events[0].kind == ScheduleEvent::Kind::Pump);
    auto rx = toSystemDrives(*ions.scheduleFor("rx", {3}, {{"theta", kPi / 2.0}}), ions);
    REQUIRE(rx);
    REQUIRE(only(*rx, "r[3]").scale == *ions.driveCoupling(3));
}

TEST_CASE("a drive envelope plugs straight into qsim::DriveTerm (spec 07 §5)") {
    const PulseLibrary& lib = library("sc_fixed_5");
    auto drives = toSystemDrives(*lib.scheduleFor("cx", {1, 2}), lib);
    REQUIRE(drives);
    std::vector<qsim::DriveTerm> terms;
    for (auto const& d : drives->drives) {
        qsim::DriveTerm term;
        term.channel = d.channel;
        term.envelope =
            d.envelope(); // std::function<Complex(double)>, the type the backend expects
        terms.push_back(std::move(term));
    }
    REQUIRE(terms.size() == drives->drives.size());
    for (std::size_t i = 0; i < terms.size(); ++i)
        for (double t : {0.0, 12.5e-9, 260e-9})
            REQUIRE(terms[i].envelope(t) == drives->drives[i].at(t));
}

TEST_CASE("toSystemDrives refuses a foreign dt and uncalibrated channels; loads from a device "
          "directory") {
    const PulseLibrary& lib = library("sc_fixed_5");
    Schedule foreign(Picoseconds{250});
    foreign.insert(Play{ChannelId::drive(0), Waveform::constant(64 * 250e-12 * 16, 0.1), {}},
                   Picoseconds{0});
    REQUIRE(toSystemDrives(foreign, lib).error().code == kErrDrive);
    const PulseLibrary& ions = library("ion_chain_11");
    Schedule global(ions.dt());
    global.insert(Play{ChannelId::globalRaman(), Waveform::constant(64e-9, 0.1), {}},
                  Picoseconds{0});
    REQUIRE(toSystemDrives(global, ions).error().code == kErrDrive);

    hw::Device detached = lib.device();
    detached.directory.clear();
    const Schedule sx = *lib.scheduleFor("sx", {1});
    REQUIRE(toSystemDrives(sx, detached).error().code == kErrDrive);
    auto fromDevice = toSystemDrives(sx, lib.device());
    REQUIRE(fromDevice);
    REQUIRE(only(*fromDevice, "d[1]").held == only(*toSystemDrives(sx, lib), "d[1]").held);
}
