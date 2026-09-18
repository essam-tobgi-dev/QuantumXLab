// Spec 12 §2–§4, §7, §13; spec 25 §3.7 — the up-conversion chain: AWG quantisation SNR, generator
// phase-noise mask, IQ mixer spurs on the spectrum analyzer (bin, power, dBc), DRAG spectrum,
// the mixer-calibration tool, and a disconnected cable.
#include "InstrTestSupport.hpp"

#include <catch2/catch_approx.hpp>

#include <numbers>

using namespace qlab;
using namespace qlab::instr;
using Catch::Approx;
using instrtest::powerOn;

namespace {
constexpr double kPi = std::numbers::pi;

// AWG ch[0] → mixer IF, generator → mixer LO, analyzer on the mixer RF output.
struct Bench {
    Awg awg;
    Generator lo;
    IqMixer mixer;
    SpectrumAnalyzer sa;
    SignalGraph graph;
    std::shared_ptr<InputHub> hub = std::make_shared<InputHub>();

    explicit Bench(std::shared_ptr<const pulse::Schedule> schedule, double awgRate = 1e9) {
        RunView run;
        run.schedule = std::move(schedule);
        run.seed = 7;
        hub->publish(run);
        hub->publish(Environment{});
        graph.addNode("awg[0].ch[0]", &awg, "ch[0]");
        graph.addNode("sg_mw[0].rf", &lo, "rf");
        graph.addNode("iq_mixer[0].rf", &mixer, "rf");
        graph.addEdge("awg[0].ch[0]", "iq_mixer[0].if");
        graph.addEdge("sg_mw[0].rf", "iq_mixer[0].lo");
        Bindings b;
        b.inputs = hub;
        b.routing = &graph;
        for (IInstrument* i : std::initializer_list<IInstrument*>{&awg, &lo, &mixer, &sa}) {
            i->bind(b);
            powerOn(*i);
        }
        mixer.attach(&awg, 0, &lo);
        REQUIRE(awg.set("sample_rate", awgRate));
        REQUIRE(lo.set("frequency", 5.0e9));
        REQUIRE(lo.set("rf_on", true));
    }
};

double snrDb(const AwgStreams& st) {
    double signal = 0.0, error = 0.0;
    for (std::size_t k = 0; k < st.ideal.size(); ++k) {
        signal += std::norm(st.ideal[k]);
        error += std::norm(st.output[k] - st.ideal[k]);
    }
    return 10.0 * std::log10(signal / error);
}
} // namespace

TEST_CASE("AWG: N-bit quantisation of a full-scale tone has SNR = 6.02 N + 1.76 dB") {
    Bench bench(instrtest::toneSchedule(100e-6, 0.999));
    REQUIRE(bench.awg.set("if_frequency", 87.31e6)); // incommensurate with f_s: the error decorrelates
    auto st = bench.awg.render(0);
    REQUIRE(st);
    REQUIRE(st->output.size() == 100000);
    REQUIRE(st->output.size() % 16 == 0); // granularity
    REQUIRE(st->bits == 14);
    const double lsb = 2.0 * 0.5 / 16384.0; // Δ = 2 V_fs / 2^14
    for (std::size_t k = 0; k < st->output.size(); k += 997) {
        REQUIRE(st->codeI[k] >= -8192);
        REQUIRE(st->codeI[k] <= 8191);
        REQUIRE(st->output[k].real() == st->codeI[k] * lsb);
        REQUIRE(std::abs(st->output[k] - st->ideal[k]) <= lsb / std::numbers::sqrt2 + 1e-15);
    }
    REQUIRE(snrDb(*st) == Approx(6.02 * 14 + 1.76).margin(1.0)); // T07 §7: 86 dB
    REQUIRE(bench.awg.set("resolution_bits", std::int64_t{16}));
    auto st16 = bench.awg.render(0);
    REQUIRE(st16);
    REQUIRE(snrDb(*st16) == Approx(6.02 * 16 + 1.76).margin(1.0)); // 98 dB

    // The trace is the quantised stream; its LSB marker is Δ.
    auto t = acquire(bench.awg, "ch[0].waveform");
    REQUIRE(t);
    REQUIRE(t->cls == FidelityClass::Exact);
    REQUIRE(t->marker("LSB")->value == Approx(2.0 * 0.5 / 65536.0).epsilon(1e-12));
    REQUIRE(t->y[1234] == st16->output[1234].real());
    REQUIRE(t->y_im[1234] == st16->output[1234].imag());
    // The envelope handed to the Lindblad backend under `apply_quantization`: within Δ/2 per quadrature.
    auto q = bench.awg.quantisedEnvelope(0);
    REQUIRE(q);
    REQUIRE(std::abs((*q)[500] - Complex{0.999, 0.0}) <= 1.0 / 65536.0);
}

TEST_CASE("generator: synthesised phase noise follows the 1/f^2 + floor mask") {
    Generator gen;
    REQUIRE(gen.set("phase_noise_dbc", -110.0));
    REQUIRE(gen.set("phase_noise_floor_dbc", -140.0));
    const PhaseNoiseMask mask = gen.mask();
    REQUIRE(mask.dbcPerHz(1e4) == Approx(10.0 * std::log10(1e-11 + 1e-14)).margin(1e-9));
    REQUIRE(mask.dbcPerHz(1e5) == Approx(10.0 * std::log10(1e-13 + 1e-14)).margin(1e-9));
    REQUIRE(mask.dbcPerHz(1e8) == Approx(-140.0).margin(0.01));

    // L(f) = S_φ(f)/2 estimated by averaged periodograms (Hann, 64 segments of 16384 at 4 MS/s).
    const double fs = 4e6;
    const std::size_t seg = 16384, segments = 64;
    core::Random rng(11);
    const std::vector<double> phi = mask.synthesize(seg * segments, fs, rng);
    const num::RealVector w = num::window(num::Window::Hann, seg);
    double w2 = 0.0;
    for (double x : w) w2 += x * x;
    std::vector<double> psd(seg / 2, 0.0); // one-sided S_φ in rad²/Hz
    for (std::size_t s = 0; s < segments; ++s) {
        num::Vector buf(seg);
        double meanPhi = 0.0;
        for (std::size_t k = 0; k < seg; ++k) meanPhi += phi[s * seg + k] / static_cast<double>(seg);
        for (std::size_t k = 0; k < seg; ++k) buf[k] = w[k] * (phi[s * seg + k] - meanPhi);
        num::fftInPlace(buf);
        for (std::size_t k = 0; k < seg / 2; ++k) psd[k] += 2.0 * std::norm(buf[k]) / (fs * w2) / static_cast<double>(segments);
    }
    auto bandDbc = [&](double f0, double f1) { // mean L over a band, against the mean of the mask
        double got = 0.0, want = 0.0;
        std::size_t count = 0;
        for (std::size_t k = 1; k < seg / 2; ++k) {
            const double f = static_cast<double>(k) * fs / static_cast<double>(seg);
            if (f < f0 || f > f1) continue;
            got += psd[k] / 2.0;
            want += std::pow(10.0, mask.dbcPerHz(f) / 10.0);
            ++count;
        }
        REQUIRE(count >= 8);
        return std::pair{10.0 * std::log10(got / static_cast<double>(count)), 10.0 * std::log10(want / static_cast<double>(count))};
    };
    for (auto [f0, f1] : {std::pair{8e3, 12e3}, std::pair{40e3, 60e3}, std::pair{1.0e6, 1.8e6}}) {
        auto [got, want] = bandDbc(f0, f1);
        INFO("band " << f0 << " .. " << f1 << " Hz: got " << got << " dBc/Hz, mask " << want);
        REQUIRE(got == Approx(want).margin(1.0));
    }
    // The mask channel reports the set value at 10 kHz.
    powerOn(gen);
    auto t = acquire(gen, "phase_noise");
    REQUIRE(t);
    REQUIRE(t->yUnit == "dBc/Hz");
    REQUIRE(t->marker("L(10 kHz)")->value == Approx(mask.dbcPerHz(1e4)));
}

TEST_CASE("spectrum analyzer: AWG tone at f_LO + f_IF in the right bin, power to 0.1 dB, spurs at the set dBc") {
    Bench bench(instrtest::toneSchedule(20e-6, 1.0));
    REQUIRE(bench.sa.set("center", 5.0e9));
    REQUIRE(bench.sa.set("span", 400e6));
    REQUIRE(bench.sa.set("rbw", 100e3));
    auto t = acquire(bench.sa, "trace");
    REQUIRE(t);
    REQUIRE(t->cls == FidelityClass::Model);
    REQUIRE(t->size() == 1001);
    REQUIRE(t->xUnit == "Hz");
    REQUIRE(t->yUnit == "dBm");
    REQUIRE(t->marker("rbw_effective")->value == Approx(100e3).epsilon(1e-3));

    // Full-scale tone: V_fs = 0.5 V into 50 Ω is +3.98 dBm at the IF, 6 dB conversion loss.
    const double expectedDbm = 10.0 * std::log10(0.5 * 0.5 / (2.0 * 50.0) / 1e-3) - 6.0;
    const double bucket = 400e6 / 1000.0;
    const Marker* peak = t->marker("peak");
    REQUIRE(peak != nullptr);
    REQUIRE(std::abs(peak->x - 5.1e9) <= bucket / 2.0); // f_LO + f_IF, the right display bin
    REQUIRE(peak->value == Approx(expectedDbm).margin(0.1));
    const std::size_t bin = static_cast<std::size_t>(std::llround((5.1e9 - t->x.front()) / bucket));
    REQUIRE(t->y[bin] == Approx(expectedDbm).margin(0.1));

    // LO leakage −40 dBc at f_LO and image −35 dBc at f_LO − f_IF (spec 12 §4 defaults).
    REQUIRE(t->marker("lo")->x == Approx(5.0e9).margin(bucket / 2.0));
    REQUIRE(t->marker("lo_dbc")->value == Approx(-40.0).margin(0.1));
    REQUIRE(t->marker("image")->x == Approx(4.9e9).margin(bucket / 2.0));
    REQUIRE(t->marker("image_dbc")->value == Approx(-35.0).margin(0.1));
    // Noise floor: DANL −150 dBm/Hz + kT in a 100 kHz RBW; the rms detector reads it.
    const double floorDbm = 10.0 * std::log10((1e-18 + 1.380649e-23 * 290.0) * t->marker("rbw_effective")->value / 1e-3);
    REQUIRE(t->marker("noise_floor")->value == Approx(floorDbm).margin(0.01));
    REQUIRE(bench.sa.set("detector", std::string("rms")));
    REQUIRE(bench.sa.set("averages", std::int64_t{16}));
    auto rms = acquire(bench.sa, "trace");
    REQUIRE(rms);
    // 5.04 … 5.08 GHz holds no tone: the analyzer floor plus the LO's phase-noise floor riding on the
    // carrier, P_c · L_floor · RBW with L_floor = −150 dBc/Hz (T07 §7) — here −102 dBm under a −100 dBm floor.
    std::vector<double> quiet(rms->y.begin() + 600, rms->y.begin() + 700);
    const double skirtDbm = expectedDbm - 150.0 + 10.0 * std::log10(t->marker("rbw_effective")->value);
    const double quietDbm = 10.0 * std::log10(std::pow(10.0, floorDbm / 10.0) + std::pow(10.0, skirtDbm / 10.0));
    REQUIRE(quietDbm == Approx(-97.87).margin(0.05));
    REQUIRE(instrtest::mean(quiet) == Approx(quietDbm).margin(0.5));
    std::vector<double> far(rms->y.begin() + 20, rms->y.begin() + 120); // 4.81 … 4.85 GHz: same physics
    REQUIRE(instrtest::mean(far) == Approx(quietDbm).margin(0.5));

    // The peak table lists the three lines, strongest first; other settings move the spurs.
    auto peaks = acquire(bench.sa, "markers");
    REQUIRE(peaks);
    REQUIRE(peaks->size() >= 3);
    REQUIRE(peaks->x[0] == Approx(5.1e9).margin(bucket));
    REQUIRE(peaks->x[1] == Approx(4.9e9).margin(bucket));
    REQUIRE(peaks->x[2] == Approx(5.0e9).margin(bucket));
    REQUIRE(bench.mixer.set("lo_leakage_dbc", -52.0));
    REQUIRE(bench.mixer.set("image_rejection_dbc", -47.5));
    REQUIRE(bench.sa.set("detector", std::string("peak")));
    auto moved = acquire(bench.sa, "trace");
    REQUIRE(moved);
    REQUIRE(moved->marker("lo_dbc")->value == Approx(-52.0).margin(0.2));
    REQUIRE(moved->marker("image_dbc")->value == Approx(-47.5).margin(0.2));
    // The zero-span marker agrees with the swept trace.
    REQUIRE(*bench.sa.markerPowerDbm(5.1e9) == Approx(expectedDbm).margin(0.02));
}

TEST_CASE("spectrum analyzer: a DRAG pulse shows a main lobe of width ~1/T and the DRAG asymmetry") {
    // β = −1/α (angular) puts the spectral zero at f + α: below the carrier the spectrum is
    // suppressed, above it enhanced, by ((1 + 2πβν)/(1 − 2πβν))² at offset ±ν.
    const double T = 32e-9, sigma = 8e-9, alphaHz = -320e6;
    const double beta = -1.0 / (2.0 * kPi * alphaHz);
    auto sched = std::make_shared<pulse::Schedule>(Picoseconds{222});
    sched->insert(pulse::Play{pulse::ChannelId::drive(0), pulse::Waveform::drag(T, sigma, beta, 0.9), Picoseconds{0}}, Picoseconds{0});
    Bench bench(sched, 4.5e9);
    REQUIRE(bench.sa.set("center", 5.1e9));
    REQUIRE(bench.sa.set("span", 300e6));
    REQUIRE(bench.sa.set("rbw", 3e6));
    REQUIRE(bench.sa.set("danl_dbm_hz", -175.0));
    REQUIRE(bench.sa.set("points", std::int64_t{601}));
    auto t = acquire(bench.sa, "trace");
    REQUIRE(t);
    const double bucket = 300e6 / 600.0;
    auto at = [&](double f) { return t->y[static_cast<std::size_t>(std::llround((f - t->x.front()) / bucket))]; };
    const double top = t->marker("peak")->value;
    // −3 dB width of the main lobe: ≈ 1/T (spec 12 §15). The lifted Gaussian of σ = T/4 has
    // 1.33/T = 41.5 MHz; the oracle is the DTFT of the envelope samples themselves.
    double lo = 5.1e9, hi = 5.1e9;
    while (at(lo) > top - 3.0) lo -= bucket;
    while (at(hi) > top - 3.0) hi += bucket;
    const std::vector<Complex> env = pulse::Waveform::drag(T, sigma, beta, 0.9).sampled(222);
    auto envelopePower = [&](double nu) { // |Σ conj(e_k) e^{−i2πν t_k}|²: the AWG plays conj(e) (spec 10 §3)
        Complex acc{};
        for (std::size_t k = 0; k < env.size(); ++k) acc += std::conj(env[k]) * std::polar(1.0, -2.0 * kPi * nu * static_cast<double>(k) * 222e-12);
        return std::norm(acc);
    };
    double pMax = 0.0, fLow = 0.0, fHigh = 0.0;
    for (double nu = -100e6; nu <= 100e6; nu += 0.25e6) pMax = std::max(pMax, envelopePower(nu));
    for (double nu = -100e6; nu <= 100e6; nu += 0.25e6)
        if (envelopePower(nu) >= pMax / 2.0) { if (fLow == 0.0) fLow = nu; fHigh = nu; }
    REQUIRE(fHigh - fLow == Approx(1.33 / T).epsilon(0.03));
    REQUIRE(hi - lo == Approx(fHigh - fLow).epsilon(0.06)); // + RBW broadening and one display bucket
    REQUIRE(hi - lo == Approx(1.0 / T).epsilon(0.4));
    for (double nu : {20e6, 40e6}) {
        const double x = 2.0 * kPi * beta * nu;
        const double wantDb = 20.0 * std::log10((1.0 + x) / (1.0 - x));
        INFO("offset " << nu << " Hz: upper/lower = " << at(5.1e9 + nu) - at(5.1e9 - nu) << " dB, expected " << wantDb);
        REQUIRE(at(5.1e9 + nu) - at(5.1e9 - nu) == Approx(wantDb).margin(0.3));
    }
}

TEST_CASE("mixer calibration tool nulls LO leakage and image and writes the AWG corrections") {
    Bench bench(instrtest::toneSchedule(20e-6, 0.8));
    REQUIRE(bench.sa.set("rbw", 100e3));
    REQUIRE(bench.mixer.loLeakageDbc() == Approx(-40.0).margin(1e-9));
    REQUIRE(bench.mixer.imageDbc() == Approx(-35.0).margin(1e-9));
    const IqImbalance imb = bench.mixer.imbalance(); // IRR ≈ (ε² + φ²)/4 (T07 §5)
    const double eps = std::pow(10.0, imb.gainDb / 20.0) - 1.0, phi = imb.phaseDeg * kPi / 180.0;
    REQUIRE(10.0 * std::log10((eps * eps + phi * phi) / 4.0) == Approx(-35.0).margin(0.2));

    MixerCalibration tool(bench.awg, 0, bench.mixer, bench.sa);
    auto rep = tool.run();
    REQUIRE(rep);
    INFO(rep->log.back());
    REQUIRE(rep->cls == FidelityClass::Model);
    REQUIRE(rep->loHz == 5.0e9);
    REQUIRE(rep->ifHz == 100e6);
    // Before: the set levels, referred to the 0.8 full-scale carrier (−40 dBc is relative to full scale).
    REQUIRE(rep->loBeforeDbc == Approx(-40.0 - 20.0 * std::log10(0.8)).margin(0.1));
    REQUIRE(rep->imageBeforeDbc == Approx(-35.0).margin(0.1));
    REQUIRE(rep->loAfterDbc < -70.0);
    REQUIRE(rep->imageAfterDbc < -70.0);
    // The corrections are in the AWG channel settings and cancel the mixer's coefficients:
    // d = −ε_LO V_fs to first order, within the DAC resolution.
    const IqCorrection c = bench.awg.correction(0);
    REQUIRE(c.offsetI == rep->correction.offsetI);
    const Complex wantOffset = -bench.mixer.leakageCoefficient() * 0.5;
    REQUIRE(std::abs(Complex{c.offsetI, c.offsetQ} - wantOffset) < 3e-4);
    REQUIRE(std::get<double>(bench.awg.get("ch[0].offset_i")) == c.offsetI);
    REQUIRE(bench.mixer.loLeakageDbc() < -70.0);
    REQUIRE(bench.mixer.imageDbc() < -70.0);
    // The swept trace confirms it.
    REQUIRE(bench.sa.set("danl_dbm_hz", -170.0));
    auto t = acquire(bench.sa, "trace");
    REQUIRE(t);
    REQUIRE(t->marker("lo_dbc")->value < -65.0);
    REQUIRE(t->marker("image_dbc")->value < -65.0);
    // The carrier is the 0.8 full-scale tone through 6 dB of conversion loss, times the small gain
    // change |α + ε_img conj(β)| of the predistortion itself (α, β = (1 ± a e^{iφ})/2, ≈ +0.1 dB here).
    const Complex gc = std::polar(c.gainRatio, c.phaseSkewRad);
    const double predistortionDb = 20.0 * std::log10(std::abs((1.0 + gc) / 2.0 + bench.mixer.imageCoefficient() * std::conj((1.0 - gc) / 2.0)));
    REQUIRE(predistortionDb == Approx(0.11).margin(0.02));
    REQUIRE(t->marker("carrier")->value == Approx(10.0 * std::log10(0.4 * 0.4 / 100.0 / 1e-3) - 6.0 + predistortionDb).margin(0.05));

    // Without a tone there is nothing to calibrate against.
    Bench silent(instrtest::toneSchedule(20e-6, 0.0));
    MixerCalibration none(silent.awg, 0, silent.mixer, silent.sa);
    auto failed = none.run();
    REQUIRE_FALSE(failed);
    REQUIRE(failed.error().code == err::NoSignal);
    REQUIRE(silent.awg.correction(0).identity());
}

TEST_CASE("IQ mixer: the RF band limit applies to the carrier f_LO + f_IF, not to the LO") {
    // The band edge is a property of the RF port, so what has to be in band is the wanted sideband.
    // An LO just inside the edge can still put the carrier outside it, and vice versa.
    Bench bench(instrtest::toneSchedule(20e-6, 1.0));
    const double rfMax = std::get<double>(bench.mixer.get("rf_max"));
    const double rfMin = std::get<double>(bench.mixer.get("rf_min"));
    REQUIRE(rfMax == 12e9);
    REQUIRE(std::get<double>(bench.awg.get("if_frequency")) == 100e6);
    SignalRequest request;
    request.samples = 256;

    // LO 50 MHz inside the upper edge: the carrier lands 50 MHz outside it.
    REQUIRE(bench.lo.set("frequency", rfMax - 50e6));
    auto high = bench.graph.signalAt("iq_mixer[0].rf", request);
    REQUIRE(high);
    REQUIRE(high->landmarks.at("carrier") == Approx(rfMax + 50e6));
    REQUIRE(high->note.find("outside the RF band") != std::string::npos);
    const double outOfBandW = meanPowerWatts(*high);

    // LO 50 MHz below the lower edge: the carrier lands 50 MHz inside it, so there is no penalty.
    REQUIRE(bench.lo.set("frequency", rfMin - 50e6));
    auto low = bench.graph.signalAt("iq_mixer[0].rf", request);
    REQUIRE(low);
    REQUIRE(low->landmarks.at("carrier") == Approx(rfMin + 50e6));
    REQUIRE(low->note.empty());
    REQUIRE(10.0 * std::log10(meanPowerWatts(*low) / outOfBandW) == Approx(40.0).margin(0.01));
}

TEST_CASE("routing: a disconnected cable leaves the downstream instrument without signal") {
    Bench bench(instrtest::toneSchedule(20e-6, 1.0));
    REQUIRE(bench.sa.set("center", 5.0e9));
    REQUIRE(bench.sa.set("span", 400e6));
    REQUIRE(bench.sa.set("rbw", 100e3));
    auto connected = acquire(bench.sa, "trace");
    REQUIRE(connected);
    const double carrier = connected->marker("carrier")->value;

    // IF cable off: the carrier and image vanish, the LO leakage stays (it needs no IF).
    REQUIRE(bench.graph.setConnected("awg[0].ch[0]->iq_mixer[0].if", false));
    auto noIf = acquire(bench.sa, "trace");
    REQUIRE(noIf);
    REQUIRE(noIf->marker("carrier")->value < carrier - 60.0);
    REQUIRE(noIf->marker("lo")->value == Approx(connected->marker("lo")->value).margin(0.2));
    // LO cable off as well: nothing is converted; the trace is the noise floor and says so.
    REQUIRE(bench.graph.setConnected("sg_mw[0].rf->iq_mixer[0].lo", false));
    auto dark = acquire(bench.sa, "trace");
    REQUIRE(dark);
    REQUIRE(dark->marker("no_signal") != nullptr);
    REQUIRE(dark->marker("peak")->value < dark->marker("noise_floor")->value + 12.0);
    REQUIRE(bench.graph.setConnected("no-such-cable", true).error().code == err::BadRouting);
    // Reconnected: the tone is back. The routing matrix round-trips through JSON.
    REQUIRE(bench.graph.setConnected("awg[0].ch[0]->iq_mixer[0].if", true));
    REQUIRE(bench.graph.setConnected("sg_mw[0].rf->iq_mixer[0].lo", true));
    REQUIRE(acquire(bench.sa, "trace")->marker("carrier")->value == Approx(carrier).margin(0.05));
    REQUIRE(bench.graph.setConnected("sg_mw[0].rf->iq_mixer[0].lo", false));
    auto restored = SignalGraph::fromJson(core::Json::parse(bench.graph.toJson().dump()));
    REQUIRE(restored);
    REQUIRE(restored->edges().size() == 2);
    REQUIRE_FALSE(restored->inputOf("iq_mixer[0].lo")->connected);
    REQUIRE(restored->inputOf("iq_mixer[0].if")->connected);
    // An edge written with an explicit empty id means the default "from->to" that addEdge
    // substitutes, so a disconnected one must still restore rather than fail as an unknown cable.
    auto blank = SignalGraph::fromJson(core::Json::parse(
        R"({"edges":[{"from":"a.out","to":"b.in","id":"","connected":false}]})"));
    REQUIRE(blank);
    REQUIRE(blank->edges().size() == 1);
    REQUIRE(blank->edges()[0].id == "a.out->b.in");
    REQUIRE_FALSE(blank->edges()[0].connected);
    REQUIRE(bench.sa.set("input", std::string("nowhere")));
    REQUIRE(acquire(bench.sa, "trace").error().code == err::BadRouting);
}
