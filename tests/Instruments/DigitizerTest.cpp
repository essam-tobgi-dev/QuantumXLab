// Spec 12 §5, §15; spec 25 §3.7; T05 (6.5)–(6.7); T07 §6, §8 — digitizer: IQ cloud separation
// against the SNR formula, integration weights, discriminator training, the calibration anchor
// (assignment matrix reproduced on the reference chain, TWPA raises F_ro), three states, faults.
#include "InstrTestSupport.hpp"

#include <catch2/catch_approx.hpp>

#include <numbers>

using namespace qlab;
using namespace qlab::instr;
using Catch::Approx;
using instrtest::powerOn;

namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;

// The worked example of T05 §6.2–6.3: κ/2π = 2.2 MHz, χ/2π = −1.11 MHz, η = 0.5 at 7.2 GHz.
ReadoutParams textbook(double photons, double windowS, double acquireDelayS) {
    ReadoutParams p;
    p.resonatorHz = 7.2e9;
    p.kappaRadS = kTwoPi * 2.2e6;
    p.chiRadS = -kTwoPi * 1.11e6;
    p.photons = photons;
    p.efficiency = 0.5;
    p.tSysK = units::consts::h.v * p.resonatorHz / (2.0 * units::consts::k_B.v) /
              p.efficiency; // η = T_q / T_sys
    p.gainDb = 86.0;
    p.toneS = acquireDelayS + windowS + 200e-9;
    p.windowS = windowS;
    p.acquireDelayS = acquireDelayS;
    return p;
}

std::unique_ptr<Digitizer> bench(const std::optional<ReadoutParams>& fixed,
                                 std::shared_ptr<InputHub> hub = nullptr) {
    auto dig = std::make_unique<Digitizer>();
    Bindings b;
    b.inputs = hub ? hub : instrtest::makeHub(Environment{});
    dig->bind(b);
    powerOn(*dig);
    if (fixed)
        dig->setReadoutOverride(0, fixed);
    return dig;
}
} // namespace

TEST_CASE("readout chain: pointer states, SNR formula and the T05 worked numbers") {
    ReadoutParams p = textbook(5.0, 500e-9, 0.0);
    // T05 (6.5): α_{0,1} = ε/(κ/2 ∓ iχ), n̄ = |α|², |α_0 − α_1|² = 4χ² n̄/(κ²/4 + χ²).
    const Complex a0 = steadyStateField(p, 0), a1 = steadyStateField(p, 1);
    REQUIRE(std::norm(a0) == Approx(5.0).epsilon(1e-12));
    REQUIRE(std::norm(a1) == Approx(5.0).epsilon(1e-12));
    const double chi2 = p.chiRadS * p.chiRadS, k2 = p.kappaRadS * p.kappaRadS / 4.0;
    REQUIRE(std::norm(a0 - a1) == Approx(4.0 * chi2 * 5.0 / (k2 + chi2)).epsilon(1e-12));
    REQUIRE(std::arg(a0 / a1) ==
            Approx(-2.0 * std::atan(-p.chiRadS / (p.kappaRadS / 2.0))).epsilon(1e-9));
    // T05 §6.3 numbers: |α_0 − α_1| = √10 = 3.16, 2ηκτ = 6.9, SNR = 8.3, P_err = 1.7e−5.
    REQUIRE(std::abs(a0 - a1) == Approx(3.16).epsilon(0.01));
    REQUIRE(2.0 * p.efficiency * p.kappaRadS * p.windowS == Approx(6.9).epsilon(0.005));
    REQUIRE(theorySnr(p) == Approx(8.3).epsilon(0.01));
    REQUIRE(overlapError(theorySnr(p)) == Approx(1.7e-5).epsilon(0.25));
    REQUIRE(snrForOverlapError(overlapError(3.7)) == Approx(3.7).epsilon(1e-9));

    // Ring-up (T07 §6): α_s(t) = α_ss (1 − e^{−κt/2} e^{±iχt}) for a step drive.
    p.toneRiseS = 0.0;
    p.toneSigmaS = 1e-12;
    const CavityResponse r = cavityResponse(p, 1e9);
    REQUIRE(r.samples() == 500);
    for (std::size_t k : {std::size_t{50}, std::size_t{200}, std::size_t{499}}) {
        const double t = static_cast<double>(k) * 1e-9;
        const Complex want0 =
            a0 * (1.0 - std::exp(-p.kappaRadS * t / 2.0) * std::polar(1.0, p.chiRadS * t));
        const Complex want1 =
            a1 * (1.0 - std::exp(-p.kappaRadS * t / 2.0) * std::polar(1.0, -p.chiRadS * t));
        REQUIRE(std::abs(r.alpha[0][k] - want0) < 1e-9);
        REQUIRE(std::abs(r.alpha[1][k] - want1) < 1e-9);
    }
    // Boxcar on the steady state reproduces the formula; the matched filter is optimal during
    // ring-up.
    ReadoutParams steady = textbook(5.0, 500e-9, 2e-6);
    const CavityResponse rs = cavityResponse(steady, 1e9);
    REQUIRE(weightedSnr(steady, rs, boxcarWeights(rs.samples())) ==
            Approx(theorySnr(steady)).epsilon(1e-3));
    const double box = weightedSnr(p, r, boxcarWeights(r.samples())),
                 matched = weightedSnr(p, r, matchedWeights(r));
    REQUIRE(matched > box);
    REQUIRE(box < theorySnr(p));
    REQUIRE(box / matched ==
            Approx(0.85).margin(0.12)); // boxcar loses SNR while the cavity rings up (T07 §6)
}

TEST_CASE("digitizer: IQ cloud separation over sigma follows the T07 SNR formula") {
    // Steady state, boxcar: the separation must equal |α_1 − α_0| √(2ηκT) (spec 25 §3.7: 3 %).
    auto dig = bench(textbook(0.6, 1e-6, 2e-6));
    REQUIRE(dig->set("integration", std::string("boxcar")));
    auto rep = dig->calibrate(0, 5000, 42);
    REQUIRE(rep);
    REQUIRE(rep->cls == FidelityClass::Statistical);
    REQUIRE(rep->snrTheory ==
            Approx(std::abs(steadyStateField(rep->params, 1) - steadyStateField(rep->params, 0)) *
                   std::sqrt(2.0 * 0.5 * rep->params.kappaRadS * 1e-6))
                .epsilon(1e-12));
    INFO("measured " << rep->snrMeasured << ", formula " << rep->snrTheory);
    REQUIRE(rep->snrTheory == Approx(4.09).epsilon(0.01));
    REQUIRE(rep->snrMeasured == Approx(rep->snrTheory).epsilon(0.03));
    REQUIRE(rep->snrExpected == Approx(rep->snrTheory).epsilon(2e-3));
    // The trained discriminator's error is the overlap integral ½ erfc(SNR/2√2) (T07 (6.3)).
    const double overlap = overlapError(rep->snrTheory);
    REQUIRE(1.0 - rep->assignment.fidelity() ==
            Approx(overlap).margin(3.0 * std::sqrt(overlap / 10000.0)));
    REQUIRE(rep->assignment.matrix[0][0] + rep->assignment.matrix[0][1] ==
            Approx(1.0).margin(1e-12)); // row-stochastic
    REQUIRE(rep->assignment.sigma[0][1] ==
            Approx(std::sqrt(overlap * (1 - overlap) / 5000.0)).epsilon(0.2));

    // SNR ∝ √n̄ and ∝ √η; averaging N shots multiplies it by √N (analytic path = time-domain path).
    auto four = bench(textbook(2.4, 1e-6, 2e-6));
    REQUIRE(four->set("integration", std::string("boxcar")));
    auto rep4 = four->calibrate(0, 4000, 43);
    REQUIRE(rep4);
    REQUIRE(rep4->snrMeasured == Approx(2.0 * rep->snrTheory).epsilon(0.03));
    REQUIRE(dig->set("averages", std::int64_t{4}));
    dig->clearCloud(0);
    auto averaged = dig->calibrate(0, 4000, 44);
    REQUIRE(averaged);
    REQUIRE(averaged->snrMeasured == Approx(2.0 * rep->snrTheory).epsilon(0.03));

    // Matched weights during ring-up: the measured separation follows the weighted prediction.
    auto ring = bench(textbook(2.0, 400e-9, 0.0));
    auto matched = ring->calibrate(0, 4000, 45);
    REQUIRE(matched);
    REQUIRE(matched->snrMeasured == Approx(matched->snrExpected).epsilon(0.03));
    REQUIRE(ring->set("integration", std::string("boxcar")));
    auto boxcar = ring->calibrate(0, 4000, 46);
    REQUIRE(boxcar);
    REQUIRE(boxcar->snrMeasured == Approx(boxcar->snrExpected).epsilon(0.03));
    REQUIRE(boxcar->snrExpected < matched->snrExpected);
}

TEST_CASE("digitizer: the ADC record is quantised and carries the chain noise k_B T_sys G") {
    auto dig = bench(textbook(1.0, 1e-6, 2e-6));
    auto rec = acquire(*dig, "trace");
    REQUIRE(rec);
    REQUIRE(rec->size() == 1000);
    const ReadoutParams p = *dig->readoutParams(0);
    const double lsb = 2.0 * 0.5 / 4096.0; // 12 bit over ±0.5 V
    for (double v : rec->y)
        REQUIRE(std::abs(v / lsb - std::round(v / lsb)) < 1e-9);
    // rms of the record = carrier power + noise: A²/2 + σ_v² (+ Δ²/12).
    const double amplitude = voltsPerRootPhoton(p) * std::abs(steadyStateField(p, 0));
    const double sigma = adcNoiseRms(p, 1e9);
    REQUIRE(sigma ==
            Approx(std::sqrt(units::consts::k_B.v * p.tSysK * std::pow(10.0, 8.6) * 50.0 * 0.5e9))
                .epsilon(1e-12));
    REQUIRE(sigma >
            8.0 * lsb); // T07 §7: the chain noise spans ≥ 8 LSB, so quantisation is negligible
    double ms = 0.0;
    for (double v : rec->y)
        ms += v * v / 1000.0;
    REQUIRE(std::sqrt(ms) ==
            Approx(std::sqrt(amplitude * amplitude / 2.0 + sigma * sigma + lsb * lsb / 12.0))
                .epsilon(0.08));
    // The demodulated voltage settles on the pointer state's envelope; the weights are matched.
    auto demod = acquire(*dig, "demod");
    REQUIRE(demod);
    REQUIRE(demod->complexValued());
    auto w = acquire(*dig, "weights");
    REQUIRE(w);
    REQUIRE(w->size() == 1000);
}

TEST_CASE("digitizer: trained assignment matrix reproduces the calibration on the reference chain; "
          "a TWPA raises F_ro") {
    const Environment shipped = instrtest::deviceEnvironment("sc_fixed_5");
    const Environment reference =
        instrtest::withPreamp(shipped, "none"); // readout_out_std, no preamp (spec 12 §15)
    const hw::Matrix2d cal = shipped.calibration->qubits[0].readoutAssignment;
    auto dig = bench(std::nullopt, instrtest::makeHub(reference));
    const ReadoutParams p = *dig->readoutParams(0);
    REQUIRE(p.resonatorHz == 6.99862e9);
    REQUIRE(p.kappaRadS == Approx(kTwoPi * 3.420986e6));
    REQUIRE(p.chiRadS == Approx(-kTwoPi * 0.5129116e6));
    REQUIRE(p.windowS == Approx(700e-9));
    REQUIRE(p.efficiency == Approx(referenceOutputChain(p.resonatorHz).efficiency).epsilon(1e-9));
    REQUIRE(p.efficiency < 0.08); // HEMT only (T07 §8: η ≈ 0.04–0.06)
    REQUIRE(p.photons > 1.0);
    REQUIRE(p.decay10 <= -std::expm1(-700e-9 / shipped.calibration->qubits[0].t1.value.v) + 1e-15);

    const std::size_t shots = 10000;
    auto rep = dig->calibrate(0, shots, 2026);
    REQUIRE(rep);
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            const double want = cal[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
            const double sigma = std::sqrt(want * (1.0 - want) / static_cast<double>(shots));
            INFO("M["
                 << i << "][" << j << "] = "
                 << rep->assignment.matrix[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]
                 << ", calibration " << want << ", sigma " << sigma);
            REQUIRE(std::abs(rep->assignment
                                 .matrix[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] -
                             want) < 3.0 * sigma);
        }
    const double fReference = rep->assignment.fidelity();
    REQUIRE(fReference == Approx(shipped.calibration->qubits[0].readoutFidelity()).margin(0.004));

    // The shipped wiring has the TWPA: higher η, same photons and transitions, better fidelity.
    auto quiet = bench(std::nullopt, instrtest::makeHub(shipped));
    const ReadoutParams pq = *quiet->readoutParams(0);
    REQUIRE(pq.photons == Approx(p.photons).epsilon(1e-12));
    REQUIRE(pq.efficiency > 3.0 * p.efficiency);
    REQUIRE(pq.efficiency < 0.5); // phase-preserving limit (T07 §8)
    auto repq = quiet->calibrate(0, shots, 2027);
    REQUIRE(repq);
    REQUIRE(repq->snrMeasured > 1.7 * rep->snrMeasured);
    REQUIRE(repq->assignment.fidelity() >= fReference + 0.01);
    // Classification uses the trained boundary; the cloud keeps both labels.
    REQUIRE(quiet->classify(0, repq->discriminator.means[1][0], repq->discriminator.means[1][1]) ==
            1);
    REQUIRE(quiet->classify(0, repq->discriminator.means[0][0], repq->discriminator.means[0][1]) ==
            0);
    REQUIRE(quiet->classify(1, 0.0, 0.0) == -1);
    auto cloud = acquire(*quiet, "ch[0].iq");
    REQUIRE(cloud);
    REQUIRE(cloud->cls == FidelityClass::Statistical);
    REQUIRE(cloud->size() == 2000); // `cloud_points`
    REQUIRE(cloud->aux.at("prepared").size() == 2000);
    REQUIRE(cloud->aux.at("assigned")[0] >= 0.0);
    REQUIRE(cloud->marker("snr")->value == repq->snrMeasured);
    REQUIRE(cloud->marker("snr_theory") != nullptr);
    REQUIRE(cloud->sigma.has_value());
    auto hist = acquire(*quiet, "histogram");
    REQUIRE(hist);
    double total = 0.0, left = 0.0;
    for (std::size_t b = 0; b < hist->size(); ++b) {
        total += hist->y[b];
        if (hist->x[b] < 0.0)
            left += hist->y[b];
    }
    REQUIRE(total == Approx(2000.0).margin(3.0));
    REQUIRE(left / total ==
            Approx(0.5).margin(0.05)); // two equal blobs either side of the threshold
}

TEST_CASE("digitizer: three-state discrimination, live run source, and the window/memory fault") {
    auto dig = bench(textbook(6.0, 1e-6, 2e-6));
    REQUIRE(dig->set("states", std::int64_t{3}));
    auto rep = dig->calibrate(0, 2000, 5);
    REQUIRE(rep);
    REQUIRE(rep->assignment.matrix.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        double row = 0.0;
        for (std::size_t j = 0; j < 3; ++j)
            row += rep->assignment.matrix[i][j];
        REQUIRE(row == Approx(1.0).margin(1e-12));
        REQUIRE(rep->assignment.matrix[i][i] > 0.95);
    }
    core::Random rng(1);
    const std::vector<std::uint8_t> two{2};
    auto leak = dig->measure(0, two, rng);
    REQUIRE(leak);
    REQUIRE((*leak)[0].assigned == 2);
    REQUIRE(dig->set("states", std::int64_t{2}));
    REQUIRE(dig->measure(0, two, rng).error().code == err::BadInput);

    // `source = run`: one labelled point per new shot of the RunView.
    auto hub = instrtest::makeHub(Environment{});
    auto live = bench(textbook(6.0, 1e-6, 2e-6), hub);
    RunView run;
    run.running = true;
    run.measuredBits = {1};
    run.shot = 0;
    hub->publish(run);
    REQUIRE(acquire(*live, "ch[0].iq")->size() == 1);
    REQUIRE(acquire(*live, "ch[0].iq")->size() == 1); // same shot: nothing new
    run.shot = 1;
    run.measuredBits = {0};
    hub->publish(run);
    auto two_points = acquire(*live, "ch[0].iq");
    REQUIRE(two_points->size() == 2);
    REQUIRE(two_points->aux.at("prepared") == std::vector<double>{1.0, 0.0});
    REQUIRE(*live->query("trigger") == 1.0);
    REQUIRE(live->query("ch[0].iq").has_value());

    // Spec 12 §1: a window longer than the record memory is a Fault with a message.
    REQUIRE(live->set("memory_samples", std::int64_t{1024}));
    REQUIRE(live->set("window_ns", 2000.0));
    REQUIRE(live->state() == State::Fault);
    REQUIRE(live->faultMessage().find("2000 samples exceeds the 1024") != std::string::npos);
    REQUIRE(acquire(*live, "ch[0].iq").error().code == err::Faulted);
    REQUIRE(live->set("window_ns", 900.0));
    REQUIRE(live->state() == State::Idle);
    REQUIRE(acquire(*live, "ch[0].iq"));
}
