// Spec 21 §3.14–§3.16 — the pulse viewer's layout (channel rows, the baseband envelope on the
// device grid, frame ticks, acquisition spans, decimation and the hover readout) and the two
// time-domain views (populations with leakage, trajectories with the ensemble mean). Headless.
#include "Data/Fidelity.hpp"
#include "Viz/Layout/PulseLayout.hpp"
#include "Viz/Views/TimeSeriesViews.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

using namespace qlab;
using namespace qlab::viz;
using Catch::Approx;

namespace {
constexpr std::int64_t kDtPs = 500; // 0.5 ns device sample period
constexpr double kPi = std::numbers::pi;
Picoseconds ns(double v) {
    return Picoseconds{static_cast<std::int64_t>(std::llround(v * 1000.0))};
}

// d[0]: a 20 ns square at 0.5, a virtual Rz (shift_phase π/2) and a 20 ns gaussian at 0.8;
// m[0]: a 40 ns readout tone; a[0]: the digitiser window; f[0]: an idle flux line.
pulse::Schedule makeSchedule() {
    pulse::Schedule s{Picoseconds{kDtPs}};
    s.addFrame({"d0", pulse::ChannelId::drive(0), 5.1e9, 0.0});
    s.insert(pulse::Play{pulse::ChannelId::drive(0), pulse::Waveform::constant(20e-9, 0.5), ns(0)},
             ns(0));
    s.insert(
        pulse::FrameOp{pulse::ChannelId::drive(0), ns(10), pulse::FrameOp::Op::SetFrequency, 5.2e9},
        ns(10));
    s.insert(
        pulse::FrameOp{pulse::ChannelId::drive(0), ns(20), pulse::FrameOp::Op::ShiftPhase, kPi / 2},
        ns(20));
    s.insert(pulse::Play{pulse::ChannelId::drive(0), pulse::Waveform::gaussian(20e-9, 5e-9, 0.8),
                         ns(20)},
             ns(20));
    s.insert(
        pulse::Play{pulse::ChannelId::measure(0), pulse::Waveform::constant(40e-9, 0.3), ns(40)},
        ns(40));
    s.insert(pulse::Acquire{pulse::ChannelId::acquire(0), ns(40), ns(30), "matched",
                            pulse::AcquireKind::Integrate, 2},
             ns(40));
    s.insert(pulse::Delay{pulse::ChannelId::flux(0), ns(0), ns(10)}, ns(0));
    return s;
}
} // namespace

TEST_CASE(
    "pulse viewer: one row per channel, the envelope on the device grid, frames and windows") {
    const pulse::Schedule s = makeSchedule();
    const layout::PulseModel m = layout::buildPulseModel(s);
    // Row order of spec 21 §3.14: drive, control, flux, measure, acquire — then by qubit.
    REQUIRE(m.rows.size() == 4);
    CHECK(m.rows[0].label == "d[0]");
    CHECK(m.rows[1].label == "f[0]");
    CHECK(m.rows[2].label == "m[0]");
    CHECK(m.rows[3].label == "a[0]");
    CHECK(m.rows[3].acquire);
    CHECK(m.dtNs == Approx(0.5));
    CHECK(m.durationNs == Approx(80.0)); // the readout tone ends last
    CHECK(m.maxAbs == Approx(0.8));

    // The envelope is the schedule's own samples, laid on the device grid at the right offsets.
    const layout::PulseRow& d = m.rows[0];
    CHECK(d.frequencyHz == Approx(5.1e9)); // the declared carrier of the channel's frame
    REQUIRE(d.trace.tNs.size() == 80);     // 40 ns of playing at 0.5 ns per sample
    CHECK_FALSE(d.trace.decimated);
    CHECK(d.trace.tNs[7] == Approx(3.5));
    CHECK(d.trace.re[0] == Approx(0.5));
    CHECK(d.trace.re[39] == Approx(0.5)); // the square runs to t = 19.5 ns
    CHECK(d.trace.im[39] == Approx(0.0));
    // The gaussian is lifted, so its peak is exactly its amplitude at T/2 = 10 ns into the pulse.
    CHECK(d.trace.re[60] == Approx(0.8));
    CHECK(d.trace.re[40] < 0.2);
    const std::vector<num::Complex> wave =
        pulse::Waveform::gaussian(20e-9, 5e-9, 0.8).sampled(kDtPs);
    REQUIRE(wave.size() == 40);
    for (std::size_t k = 0; k < wave.size(); ++k)
        CHECK(d.trace.re[40 + k] == Approx(wave[k].real()));

    // Frame marks: the virtual Rz is a phase tick, the retuning a frequency annotation.
    REQUIRE(d.phaseJumps.size() == 1);
    CHECK(d.phaseJumps[0].tNs == Approx(20.0));
    CHECK(d.phaseJumps[0].deltaPhase == Approx(kPi / 2));
    CHECK(d.phaseJumps[0].phase == Approx(kPi / 2));
    REQUIRE(d.frequencyChanges.size() == 1);
    CHECK(d.frequencyChanges[0].tNs == Approx(10.0));
    CHECK(d.frequencyChanges[0].frequencyHz == Approx(5.2e9));
    REQUIRE(m.rows[3].acquisitions.size() == 1);
    CHECK(m.rows[3].acquisitions[0].t0Ns == Approx(40.0));
    CHECK(m.rows[3].acquisitions[0].t1Ns == Approx(70.0));
    CHECK(m.rows[3].acquisitions[0].memorySlot == 2);
    CHECK(m.rows[1].trace.tNs.empty()); // a channel that only idles keeps its row and draws nothing

    // Hover readout (spec 21 §3.14): sample value, frame phase and channel frequency at the cursor.
    auto early = layout::readoutAt(m, 0, 5.0);
    REQUIRE(early.has_value());
    CHECK(early->re == Approx(0.5));
    CHECK(early->framePhase == Approx(0.0));
    CHECK(early->frequencyHz == Approx(5.1e9)); // before the retuning
    CHECK_FALSE(early->inAcquisition);
    auto late = layout::readoutAt(m, 0, 30.0);
    REQUIRE(late.has_value());
    CHECK(late->framePhase == Approx(kPi / 2));
    CHECK(late->frequencyHz == Approx(5.2e9));
    CHECK(layout::readoutAt(m, 3, 50.0)->inAcquisition);
    CHECK_FALSE(layout::readoutAt(m, 3, 75.0)->inAcquisition);
    CHECK_FALSE(layout::readoutAt(m, 9, 1.0).has_value());
}

TEST_CASE("pulse viewer: zoom sets the decimation, and the qubit selection filters the channels") {
    const pulse::Schedule s = makeSchedule();
    layout::PulseLayoutOptions o;
    o.maxPoints = 8; // spec 22 §2: below the sample count, a row keeps the min/max envelope instead
    const layout::PulseModel m = layout::buildPulseModel(s, o);
    const layout::PulseTrace& t = m.rows[0].trace;
    CHECK(t.decimated);
    CHECK(t.sourceSamples == 80);
    REQUIRE(t.tNs.size() == 8);
    REQUIRE(t.reHi.size() == 8);
    CHECK(t.maxAbs == Approx(0.8)); // the extent survives the decimation, unlike a plain resample
    double hi = 0.0;
    for (std::size_t k = 0; k < t.tNs.size(); ++k) {
        CHECK(t.reLo[k] <= t.re[k] + 1e-12);
        CHECK(t.re[k] <= t.reHi[k] + 1e-12);
        hi = std::max(hi, t.reHi[k]);
    }
    CHECK(hi == Approx(0.8));

    const QubitIndex other[1] = {QubitIndex{1}};
    layout::PulseLayoutOptions filtered;
    filtered.qubits = other;
    CHECK(layout::buildPulseModel(s, filtered).rows.empty()); // every channel here is on qubit 0
}

TEST_CASE(
    "populations vs time: P0, P1 and the leakage curve of one site, with the drive underneath") {
    // A driven qutrit: P0 = cos²(Ωt/2), P1 = sin²(Ωt/2) minus the leakage that grows into |2⟩.
    auto series = std::make_shared<LindbladSeries>();
    series->sites = 2;
    series->levels = 3;
    const double omega = 2.0 * kPi * 25e6; // 25 MHz Rabi rate
    for (int k = 0; k <= 40; ++k) {
        const double tS = k * 1e-9;
        const double leak = 0.002 * static_cast<double>(k);
        const double c = std::cos(0.5 * omega * tS), sn = std::sin(0.5 * omega * tS);
        qsim::TimeSample sample;
        sample.timeS = tS;
        sample.populations = {(1.0 - leak) * c * c, (1.0 - leak) * sn * sn, leak, 1.0, 0.0, 0.0};
        sample.leakage = leak;
        series->samples.push_back(std::move(sample));
        series->purity.push_back(1.0 - 0.001 * k);
    }
    series->blochNorm = {{}, {}};
    for (int k = 0; k <= 40; ++k)
        series->blochNorm[0].push_back(1.0 - 0.001 * k);

    ViewInput in;
    in.lindblad = series;
    const pulse::Schedule schedule = makeSchedule();
    in.schedule = borrow(schedule);
    PopulationsView view;
    view.update(in);
    CHECK(view.id() == "populations");
    CHECK(view.fidelity(in) == data::FidelityClass::Numerical);
    REQUIRE(view.levels() == 3);
    REQUIRE(view.timeNs().size() == 41);
    CHECK(view.timeNs()[10] == Approx(10.0));
    // At t = 0 the qubit is in |0⟩; the leakage curve is the third level and grows monotonically.
    CHECK(view.level(0)[0] == Approx(1.0));
    CHECK(view.level(1)[0] == Approx(0.0));
    CHECK(view.level(2)[0] == Approx(0.0));
    CHECK(view.level(2)[40] == Approx(0.08));
    for (std::size_t k = 1; k < 41; ++k)
        CHECK(view.level(2)[k] >= view.level(2)[k - 1]);
    // The three populations of each sample sum to one: the leakage is shown, not renormalised away.
    for (std::size_t k = 0; k < 41; ++k)
        CHECK(view.level(0)[k] + view.level(1)[k] + view.level(2)[k] == Approx(1.0).margin(1e-12));
    // Half a Rabi period (Ωt = π) at t = 20 ns inverts the qubit, up to the leakage lost by then.
    CHECK(view.level(1)[20] == Approx(1.0 - 0.04).margin(1e-9));

    const auto csv = view.exportCsv();
    REQUIRE(csv.has_value());
    CHECK(csv->rfind("t_ns,P0,P1,P2,purity,bloch_norm\n", 0) == 0);
    view.setSite(1); // the second site is idle in |0⟩ throughout
    CHECK(view.level(0)[40] == Approx(1.0));
    CHECK(view.site() == 1);
}

TEST_CASE("trajectory view: at most 256 traces are drawn, the mean keeps every trajectory") {
    auto e = std::make_shared<TrajectoryEnsemble>();
    e->qubit = 2;
    e->trajectories = 1000;
    for (int k = 0; k <= 20; ++k) {
        const double tS = k * 5e-9;
        e->timeS.push_back(tS);
        e->meanZ.push_back(std::exp(-tS / 40e-9)); // T1 decay of ⟨Z⟩ toward 0
        e->stderrZ.push_back(0.03);
    }
    for (int j = 0; j < 300; ++j) {
        TrajectoryTrace t;
        for (int k = 0; k <= 20; ++k) {
            t.timeS.push_back(k * 5e-9);
            t.z.push_back(k * 5 < 50 + j % 40 ? 1.0 : -1.0); // one jump per trajectory
        }
        t.jumpTimesS.push_back((50.0 + j % 40) * 1e-9);
        e->traces.push_back(std::move(t));
    }
    ViewInput in;
    in.trajectories = e;
    TrajectoryView view;
    view.update(in);
    CHECK(view.fidelity(in) == data::FidelityClass::Statistical);
    CHECK(view.drawnTraces() == TrajectoryView::kMaxDrawn); // 256 of 300 drawn (spec 21 §3.16)
    CHECK(view.trajectories() == 1000);                     // the mean keeps all of them
    REQUIRE(view.meanZ().size() == 21);
    CHECK(view.timeNs()[4] == Approx(20.0));
    CHECK(view.meanZ()[0] == Approx(1.0));
    CHECK(view.meanZ()[8] == Approx(std::exp(-1.0))); // t = 40 ns = T1
    CHECK(view.statusLine().find("1000") != std::string::npos);

    // ⟨Z⟩ = P₀ − P₁ from the backend's averaged samples, with the two errors added.
    std::vector<qsim::AveragedSample> avg(2);
    avg[0].timeS = 0.0;
    avg[0].populations = {1.0, 0.0};
    avg[0].stderrs = {0.0, 0.0};
    avg[1].timeS = 1e-8;
    avg[1].populations = {0.7, 0.3};
    avg[1].stderrs = {0.02, 0.02};
    const TrajectoryEnsemble from = ensembleFromAverages(avg, 0, 2, 500);
    REQUIRE(from.meanZ.size() == 2);
    CHECK(from.meanZ[0] == Approx(1.0));
    CHECK(from.meanZ[1] == Approx(0.4));
    CHECK(from.stderrZ[1] == Approx(0.04));
    CHECK(from.trajectories == 500);
}
