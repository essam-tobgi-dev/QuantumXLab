// Spec 12 §6, §15; spec 25 §3.7; T05 §6 — VNA: notch fit recovery at 40 dB SNR, the dispersive
// shift 2χ from ground/excited sweeps, the live population mixture, trace noise from IFBW and
// averages, TLS power dependence, and the sweep-range fault.
#include "InstrTestSupport.hpp"

#include <catch2/catch_approx.hpp>

#include <numbers>

using namespace qlab;
using namespace qlab::instr;
using Catch::Approx;
using instrtest::powerOn;

namespace {
std::unique_ptr<Vna> bench(std::shared_ptr<InputHub> hub) {
    auto vna = std::make_unique<Vna>();
    Bindings b;
    b.inputs = std::move(hub);
    vna->bind(b);
    powerOn(*vna);
    return vna;
}

// Sets the source power so that the trace SNR (baseline over complex noise) is `snrDb`.
void setSnr(Vna& vna, double snrDb) {
    REQUIRE(vna.set("fit", false));
    auto probe = acquire(vna, "s21");
    REQUIRE(probe);
    const double now = probe->marker("snr_db")->value;
    const double power = std::get<double>(vna.get("power")) + (snrDb - now);
    REQUIRE(power >= -60.0);
    REQUIRE(power <= 0.0);
    REQUIRE(vna.set("power", power));
    REQUIRE(vna.set("fit", true));
    REQUIRE(vna.reports().empty()); // nothing was clamped on the way
}
} // namespace

TEST_CASE("VNA: notch fit of a synthesised S21 recovers f_r to 1e-7 and Q_i, Q_c to 2 % at 40 dB SNR") {
    auto vna = bench(instrtest::makeHub(Environment{}));
    NotchResonator r;
    r.frHz = 7.0123e9;
    r.qInternal = 2.0e5;
    r.qCoupling = 1.0e4;
    r.phi = 0.12;
    vna->setResonators(std::vector<NotchResonator>{r});
    REQUIRE(vna->set("averages", std::int64_t{1}));
    // 20001 points: at 40 dB (1 % amplitude noise) the 1σ of Q_i is then 0.6 % and of f_r 150 Hz,
    // so the 2 % and 10⁻⁷ oracles of spec 25 §3.7 hold with > 3σ margin rather than by luck.
    REQUIRE(vna->set("points", std::int64_t{20001}));
    setSnr(*vna, 40.0);
    auto t = acquire(*vna, "s21");
    REQUIRE(t);
    REQUIRE(t->marker("snr_db")->value == Approx(40.0).margin(0.05));
    REQUIRE(t->size() == 20001);
    const double ql = 1.0 / (1.0 / r.qInternal + std::cos(r.phi) / r.qCoupling);
    // The dip sits next to f_r. In the complex plane the notch is a circle of diameter d = Q_l/|Q_c|
    // through the off-resonant point 1, centred at 1 − (d/2) e^{iφ}: its closest approach to the
    // origin, |centre| − d/2, is the depth (−26 dB here; the 40 dB noise moves it by a dB or two).
    const std::size_t dip = static_cast<std::size_t>(std::min_element(t->y.begin(), t->y.end()) - t->y.begin());
    REQUIRE(t->x[dip] == Approx(r.frHz).epsilon(2e-5));
    const double d = ql / r.qCoupling;
    const double depthDb = 20.0 * std::log10(std::abs(1.0 - 0.5 * d * std::exp(Complex{0.0, r.phi})) - 0.5 * d);
    REQUIRE(depthDb == Approx(-26.4).margin(0.1));
    REQUIRE(t->y[dip] - t->y[5] == Approx(depthDb).margin(3.0));

    const ResonatorFit fit = *vna->lastFit();
    INFO("f_r " << fit.frHz << " ± " << fit.sigmaFrHz << ", Q_i " << fit.qi << " ± " << fit.sigmaQi << ", Q_c " << fit.qc << " ± " << fit.sigmaQc);
    REQUIRE(fit.converged);
    REQUIRE(fit.cls == FidelityClass::Statistical);
    REQUIRE(std::abs(fit.frHz - r.frHz) / r.frHz < 1e-7);
    REQUIRE(fit.qc == Approx(r.qCoupling).epsilon(0.02));
    REQUIRE(fit.qi == Approx(r.qInternal).epsilon(0.02));
    REQUIRE(fit.ql == Approx(ql).epsilon(0.005));
    REQUIRE(fit.phi == Approx(r.phi).margin(0.01));
    REQUIRE(fit.kappaHz == Approx(r.frHz / ql).epsilon(0.005)); // κ/2π = f_r/Q_l
    REQUIRE(fit.chi2ndf == Approx(1.0).margin(0.15));           // the stated σ is the real scatter
    // The quoted uncertainties are honest: every parameter within 4σ.
    REQUIRE(std::abs(fit.frHz - r.frHz) < 4.0 * fit.sigmaFrHz);
    REQUIRE(std::abs(fit.qi - r.qInternal) < 4.0 * fit.sigmaQi);
    REQUIRE(std::abs(fit.qc - r.qCoupling) < 4.0 * fit.sigmaQc);
    REQUIRE(fit.sigmaQi / fit.qi < 0.0075);
    REQUIRE(fit.sigmaFrHz < 250.0);
    // Environment: the cable delay τ = L/(0.7 c) of the default 13.6 m path and the net gain a.
    REQUIRE(fit.tauS == Approx(13.6 / (0.7 * 299792458.0)).epsilon(0.02));
    // Markers carry the fit onto the trace; the other channels show the same sweep.
    REQUIRE(t->marker("f_r")->value == fit.frHz);
    REQUIRE(t->marker("Q_i")->sigma == fit.sigmaQi);
    REQUIRE(t->marker("kappa")->unit == "Hz");
    // The `span` and `trace` binding paths report the sweep that was actually taken. With
    // `auto_span` on (the default) the range comes from the resonators, not from f_start/f_stop.
    REQUIRE(std::get<bool>(vna->get("auto_span")));
    REQUIRE(*vna->query("span") == Approx(t->x.back() - t->x.front()).epsilon(1e-12));
    REQUIRE(*vna->query("span") != Approx(std::get<double>(vna->get("f_stop")) - std::get<double>(vna->get("f_start"))));
    REQUIRE(*vna->query("trace") == Approx(*std::min_element(t->y.begin(), t->y.end())).epsilon(1e-12)); // notch depth, dB
    REQUIRE(*vna->query("trace") < -20.0);
    REQUIRE(*vna->query("Q_i") == fit.qi); // the Q stays on its own path
    auto circle = acquire(*vna, "circle");
    REQUIRE(circle);
    REQUIRE(circle->size() == 20001);
    auto phase = acquire(*vna, "s21_phase");
    REQUIRE(phase);
    REQUIRE(phase->yUnit == "deg");
    REQUIRE(*vna->query("f_r") == Approx(r.frHz).epsilon(1e-6));

    // Spec 12 §6's harder case, Q_i = 10⁶ behind Q_c = 10⁴ at 30 dB: Q_i is the small difference
    // 1/Q_l − cos φ/Q_c, so its relative error is that of Q_l amplified by Q_i/Q_l ≈ 100 — about
    // 9 % (1σ) for one 20001-point sweep, which no estimator can beat. The fit stays unbiased, says
    // how uncertain it is, and is well inside 2 % once power and averaging bring the trace to 60 dB.
    r.qInternal = 1.0e6;
    r.phi = 0.0;
    vna->setResonators(std::vector<NotchResonator>{r});
    setSnr(*vna, 30.0);
    REQUIRE(acquire(*vna, "s21"));
    const ResonatorFit hard = *vna->lastFit();
    INFO("hard case: Q_i " << hard.qi << " ± " << hard.sigmaQi << ", Q_c " << hard.qc << " ± " << hard.sigmaQc);
    REQUIRE(hard.qc == Approx(1.0e4).epsilon(0.02));
    REQUIRE(std::abs(hard.frHz - r.frHz) < 4.0 * hard.sigmaFrHz);
    REQUIRE(std::abs(hard.qi - 1.0e6) < 4.0 * hard.sigmaQi);
    REQUIRE(hard.sigmaQi / hard.qi == Approx(0.09).margin(0.04));
    setSnr(*vna, 45.0);
    REQUIRE(vna->set("averages", std::int64_t{30})); // +14.8 dB
    REQUIRE(acquire(*vna, "s21"));
    const ResonatorFit averaged = *vna->lastFit();
    INFO("averaged: Q_i " << averaged.qi << " ± " << averaged.sigmaQi);
    REQUIRE(averaged.sigmaQi / averaged.qi < 0.005);
    REQUIRE(averaged.qi == Approx(1.0e6).epsilon(0.02));
}

TEST_CASE("VNA: ground and excited sweeps of sc_fixed_5 give the dispersive shift 2 chi to 2 %") {
    const Environment env = instrtest::deviceEnvironment("sc_fixed_5");
    auto hub = instrtest::makeHub(env);
    auto vna = bench(hub);
    const auto resonators = *vna->resonators();
    REQUIRE(resonators.size() == 5); // one feedline, five resonators
    REQUIRE(resonators[0].frHz == 6.99862e9);
    REQUIRE(resonators[0].loadedQ() == Approx(6.99862e9 / 3.420986e6).epsilon(1e-9));
    REQUIRE(resonators[0].criticalPhotons > 10.0);
    REQUIRE(vna->set("fit_qubit", std::int64_t{0}));
    REQUIRE(vna->set("points", std::int64_t{8001}));
    REQUIRE(vna->set("averages", std::int64_t{1}));
    REQUIRE(vna->set("qubit_state", std::string("0")));
    setSnr(*vna, 40.0);
    auto ground = acquire(*vna, "s21");
    REQUIRE(ground);
    // auto_span: all five notches are on screen.
    REQUIRE(ground->x.front() < 6.99862e9);
    REQUIRE(ground->x.back() > 7.17963e9);
    const ResonatorFit f0 = *vna->lastFit();
    REQUIRE(vna->set("qubit_state", std::string("1")));
    REQUIRE(acquire(*vna, "s21"));
    const ResonatorFit f1 = *vna->lastFit();
    const double chi = -0.5129116e6; // calibration readout_chi_mhz
    // T05 (6.2): ω_r − χ in |0⟩, ω_r + χ in |1⟩; χ < 0 pushes the ground-state resonator up.
    REQUIRE(f0.frHz > f1.frHz);
    REQUIRE(f1.frHz - f0.frHz == Approx(2.0 * chi).epsilon(0.02));
    // The midpoint is the bare f_r, up to the pull of the neighbouring notch 47 MHz away, whose tail
    // tilts the background of this single-notch fit: a fraction of a percent of the 3.4 MHz linewidth.
    REQUIRE(0.5 * (f0.frHz + f1.frHz) == Approx(6.99862e9).margin(0.01 * 3.420986e6));
    REQUIRE(f0.kappaHz == Approx(3.420986e6).epsilon(0.02)); // κ/2π = f_r/Q_l recovers readout_kappa_mhz

    // Live mode: the population-weighted mixture p0 S21(0) + p1 S21(1) of the run snapshot.
    REQUIRE(vna->set("power", 0.0));
    REQUIRE(vna->set("if_bandwidth", 1.0));
    REQUIRE(vna->set("averages", std::int64_t{1000})); // noise ≈ −115 dB: compare traces directly
    REQUIRE(vna->set("fit", false));
    auto pure1 = acquire(*vna, "s21_complex");
    REQUIRE(vna->set("qubit_state", std::string("0")));
    auto pure0 = acquire(*vna, "s21_complex");
    auto snap = std::make_shared<qsim::Snapshot>();
    snap->nQubits = 5;
    snap->amplitudes = std::vector<Complex>(32, Complex{});
    (*snap->amplitudes)[0] = std::sqrt(0.7); // q0: √0.7 |0⟩ + √0.3 |1⟩, the others in |0⟩
    (*snap->amplitudes)[1] = std::sqrt(0.3);
    RunView run;
    run.nQubits = 5;
    run.state = snap;
    hub->publish(run);
    REQUIRE(vna->set("qubit_state", std::string("live")));
    auto live = acquire(*vna, "s21_complex");
    REQUIRE(live);
    REQUIRE(pure0);
    REQUIRE(pure1);
    const double a = std::hypot(pure0->y[0], pure0->y_im[0]);
    for (std::size_t i = 0; i < live->size(); i += 37) {
        REQUIRE(live->y[i] == Approx(0.7 * pure0->y[i] + 0.3 * pure1->y[i]).margin(1e-4 * a));
        REQUIRE(live->y_im[i] == Approx(0.7 * pure0->y_im[i] + 0.3 * pure1->y_im[i]).margin(1e-4 * a));
    }
}

TEST_CASE("VNA: trace noise follows k_B T_sys IFBW / averages; TLS option; sweep-range fault") {
    auto vna = bench(instrtest::makeHub(Environment{}));
    NotchResonator r;
    r.frHz = 6.5e9;
    r.qInternal = 1.0e5;
    r.qCoupling = 2.0e4;
    vna->setResonators(std::vector<NotchResonator>{r});
    REQUIRE(vna->set("fit", false));
    REQUIRE(vna->set("if_bandwidth", 1e3));
    REQUIRE(vna->set("averages", std::int64_t{10}));
    auto base = acquire(*vna, "s21_complex");
    REQUIRE(base);
    const double sigma = base->sigma->y[0];
    // Per quadrature, from first principles for the default chain and −30 dBm at the port:
    //   σ² = [a² k_B T_sys / P_chip + N_rx / P_port] · IFBW / (2 · averages),
    // N_rx the receiver floor of a 120 dB dynamic range at 10 Hz IFBW below 0 dBm: −130 dBm/Hz.
    const Environment env;
    const OutputChain chain = env.outputChain(0, base->x[base->size() / 2]);
    const double att = env.inputAttenuationDb(0, base->x[base->size() / 2]);
    const double a2 = std::pow(10.0, (chain.gainDb - att) / 10.0);
    const double chainTerm = a2 * units::consts::k_B.v * chain.tSysK / (1e-3 * std::pow(10.0, (-30.0 - att) / 10.0));
    const double receiverTerm = 1e-3 * std::pow(10.0, -13.0) / (1e-3 * std::pow(10.0, -3.0));
    const double want = std::sqrt((chainTerm + receiverTerm) * 1e3 / (2.0 * 10.0));
    REQUIRE(sigma == Approx(want).epsilon(1e-9));
    REQUIRE(receiverTerm < chainTerm); // the fridge chain, not the analyzer, limits this measurement
    // Scatter of the off-resonant points: point-to-point differences of |S21| (insensitive to the
    // cable-delay rotation and to the slow notch tail) have standard deviation √2 σ.
    std::vector<double> steps;
    for (std::size_t i = 0; i + 1 < 300; ++i)
        steps.push_back(std::hypot(base->y[i + 1], base->y_im[i + 1]) - std::hypot(base->y[i], base->y_im[i]));
    REQUIRE(instrtest::stddev(steps) == Approx(std::numbers::sqrt2 * sigma).epsilon(0.15));
    REQUIRE(vna->set("if_bandwidth", 1e5));
    REQUIRE(vna->set("averages", std::int64_t{40}));
    REQUIRE(acquire(*vna, "s21_complex")->sigma->y[0] == Approx(sigma * std::sqrt(100.0 / 4.0)).epsilon(1e-9));

    // TLS saturation (Model): Q_i(n̄) = Q_i0 F / (1 + (F − 1)/√(1 + n̄/n_c)) rises from Q_i0 toward F Q_i0.
    REQUIRE(vna->set("if_bandwidth", 10.0));
    REQUIRE(vna->set("averages", std::int64_t{100}));
    REQUIRE(vna->set("tls_model", true));
    REQUIRE(vna->set("tls_factor", 4.0));
    REQUIRE(vna->set("fit", true));
    REQUIRE(vna->set("power", -60.0));
    auto low = acquire(*vna, "s21");
    REQUIRE(low);
    const double nLow = low->marker("photons")->value;
    const double qiLow = vna->lastFit()->qi;
    REQUIRE(vna->set("power", 0.0));
    auto high = acquire(*vna, "s21");
    const double nHigh = high->marker("photons")->value;
    const double qiHigh = vna->lastFit()->qi;
    auto model = [](double n) { return 1.0e5 * 4.0 / (1.0 + 3.0 / std::sqrt(1.0 + n / 1.0)); };
    REQUIRE(nHigh > 1e5 * nLow);
    REQUIRE(qiLow == Approx(model(nLow)).epsilon(0.03));
    REQUIRE(qiHigh == Approx(model(nHigh)).epsilon(0.03));
    REQUIRE(qiHigh / qiLow > 2.0);
    REQUIRE(qiHigh / qiLow < 5.0); // spec 12 §6: a factor 2–5
    REQUIRE(vna->set("tls_model", false));
    REQUIRE(acquire(*vna, "s21"));
    REQUIRE(vna->lastFit()->qi == Approx(1.0e5).epsilon(0.03));

    // A sweep that stops below its start is a settings conflict: Fault with a message (spec 12 §1).
    REQUIRE(vna->set("auto_span", false));
    REQUIRE(vna->set("f_start", 7.5e9));
    REQUIRE(vna->state() == State::Fault);
    REQUIRE(vna->faultMessage().find("not above the start") != std::string::npos);
    REQUIRE(vna->set("f_stop", 7.6e9));
    REQUIRE(vna->state() == State::Idle);
    auto flat = acquire(*vna, "s21"); // no resonator in 7.5–7.6 GHz: the fit reports why it failed
    REQUIRE(flat);
    REQUIRE(flat->markers.back().label.find("fit_failed") == 0);
}
