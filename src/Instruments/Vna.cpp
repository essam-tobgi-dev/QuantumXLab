#include "Instruments/Vna.hpp"
#include "Instruments/StateAccess.hpp"
#include "Units/Units.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

namespace qlab::instr {
namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr Complex kI{0.0, 1.0};
constexpr double kBaselinePhase =
    0.7; // θ of the chain, an arbitrary constant the fit must absorb (Model)
} // namespace

double NotchResonator::loadedQ(double qi) const {
    return 1.0 / (1.0 / qi + std::cos(phi) / qCoupling);
}

Complex notchTerm(double fHz, double frHz, double ql, double qc, double phi) {
    return (ql / qc) * std::exp(kI * phi) / (1.0 + 2.0 * kI * ql * (fHz - frHz) / frHz);
}

SettingSchema Vna::makeSchema() {
    SettingSchema s;
    s.id = "instr/vna.schema.json";
    s.instrument = "vna";
    s.settings = {
        SettingSpec::boolean("auto_span", true,
                             "Sweep around the resonators of the selected feedline"),
        SettingSpec::real("f_start", "Hz", 10e6, 9e9, 1.0, 6.9e9, "Sweep start (10 MHz – 9 GHz)"),
        SettingSpec::real("f_stop", "Hz", 10e6, 9e9, 1.0, 7.3e9, "Sweep stop"),
        SettingSpec::integer("points", "", 101, 20001, 1601, "Sweep points"),
        SettingSpec::real("power", "dBm", -60.0, 0.0, 0.1, -30.0, "Source power at the VNA port"),
        SettingSpec::real("if_bandwidth", "Hz", 1.0, 1e6, 0.0, 1e3, "IF bandwidth"),
        SettingSpec::integer("averages", "", 1, 1000, 10, "Sweep averages"),
        SettingSpec::real("dynamic_range_db", "dB", 100.0, 130.0, 0.1, 120.0,
                          "Receiver dynamic range at 10 Hz IF bandwidth (0 dBm maximum input)"),
        SettingSpec::choice("qubit_state", {"0", "1", "live"}, "live",
                            "Qubit state during the sweep: all in |0>, the qubit under test "
                            "(`fit_qubit`, or all) in |1>, or the run snapshot"),
        SettingSpec::integer("feedline", "", 0, 63, 0, "Feedline under test"),
        SettingSpec::real("q_internal", "", 1e3, 1e8, 0.0, 5e5,
                          "Internal quality factor of the readout resonators at low power (Model)"),
        SettingSpec::real("coupling_phase", "rad", -1.0, 1.0, 0.0, 0.0,
                          "Impedance-mismatch angle φ of the resonators (Model)"),
        SettingSpec::boolean("tls_model", false,
                             "TLS loss saturation: Q_i rises with circulating power (Model)"),
        SettingSpec::real("tls_factor", "", 2.0, 5.0, 0.0, 3.0,
                          "Q_i(high power) / Q_i(single photon)"),
        SettingSpec::real("tls_critical_photons", "", 0.01, 1e4, 0.0, 1.0,
                          "TLS saturation photon number n_c"),
        SettingSpec::boolean("punch_out", false, "Dispersive shift collapses above n_crit (Model)"),
        SettingSpec::boolean("fit", true, "Run the built-in notch fit and place its markers"),
        SettingSpec::integer("fit_qubit", "", -1, 1023, -1,
                             "Resonator to fit by qubit index; −1 = the deepest notch in the span"),
        refreshRateSetting(),
    };
    return s;
}

Vna::Vna(std::uint32_t index) : InstrumentBase({"vna", index}, makeSchema()) {
    setChannels({
        {{}, "s21", "Hz", "dB", FidelityClass::Model, false, false, "|S21| in dB"},
        {{}, "s21_phase", "Hz", "deg", FidelityClass::Model, false, false, "arg S21"},
        {{},
         "circle",
         "",
         "",
         FidelityClass::Model,
         false,
         false,
         "S21 in the complex plane: x = Re, y = Im"},
        {{}, "s21_complex", "Hz", "", FidelityClass::Model, true, false, "S21: y = Re, y_im = Im"},
    });
}

std::string Vna::conflict(const SettingValues& v) const {
    if (!v.flag("auto_span") && v.real("f_stop") <= v.real("f_start"))
        return std::format("sweep stop {:.6g} Hz is not above the start {:.6g} Hz",
                           v.real("f_stop"), v.real("f_start"));
    return {};
}

void Vna::setResonators(std::optional<std::vector<NotchResonator>> resonators) {
    std::lock_guard lk(fitMu_);
    override_ = std::move(resonators);
}

std::optional<ResonatorFit> Vna::lastFit() const {
    std::lock_guard lk(fitMu_);
    return lastFit_;
}

Result<std::vector<NotchResonator>> Vna::resonators() const {
    {
        std::lock_guard lk(fitMu_);
        if (override_)
            return *override_;
    }
    auto env = environment();
    if (!env || !env->device || !env->calibration)
        return fail(err::NotBound, id().toString() + ": no device and calibration are bound");
    const SettingValues v = snapshot();
    const auto line = static_cast<std::size_t>(v.integer("feedline"));
    const auto& feedlines = env->device->readout.feedlines;
    if (line >= feedlines.size())
        return fail(err::BadInput,
                    std::format("{}: the device has no feedline {}", id().toString(), line));
    std::vector<NotchResonator> out;
    for (std::uint32_t q : feedlines[line].qubits) {
        const hw::QubitCal* qc = env->calibration->qubit(q);
        if (!qc || !qc->readoutFrequency || !qc->readoutKappa)
            continue;
        NotchResonator r;
        r.qubit = q;
        r.frHz = qc->readoutFrequency->value.v;
        r.phi = v.real("coupling_phase");
        const double ql = r.frHz / qc->readoutKappa->value.v; // κ/2π = f_r/Q_l
        r.qInternal = std::max(v.real("q_internal"), 1.05 * ql);
        r.qCoupling = std::cos(r.phi) / (1.0 / ql - 1.0 / r.qInternal);
        r.chiHz = qc->readoutChi ? qc->readoutChi->value.v : 0.0;
        if (r.chiHz != 0.0) { // n_crit = Δ²/4g² with g²/Δ = χ(Δ + α)/α  (T05 (6.3), (6.4))
            const double delta = qc->f01.value.v - r.frHz, alpha = qc->anharmonicity.value.v;
            if (alpha != 0.0 && delta + alpha != 0.0)
                r.criticalPhotons = std::abs(delta * alpha / (4.0 * r.chiHz * (delta + alpha)));
        }
        out.push_back(r);
    }
    if (out.empty())
        return fail(err::BadInput, std::format("{}: feedline {} has no calibrated resonators",
                                               id().toString(), line));
    return out;
}

Result<Vna::Sweep> Vna::sweep(AcquireContext& ctx) const {
    const SettingValues& v = ctx.settings;
    Sweep sw;
    QXL_TRY_ASSIGN(sw.resonators, resonators());
    const Environment fallback;
    const Environment& env = ctx.env ? *ctx.env : fallback;
    double fStart = v.real("f_start"), fStop = v.real("f_stop");
    if (v.flag("auto_span")) { // "around the feedline resonators"
        double lo = sw.resonators.front().frHz, hi = lo, widest = 0.0;
        for (auto const& r : sw.resonators) {
            lo = std::min(lo, r.frHz);
            hi = std::max(hi, r.frHz);
            widest = std::max(widest, r.frHz / r.loadedQ() + 2.0 * std::abs(r.chiHz));
        }
        const double margin = std::max(10.0 * widest, 0.05 * (hi - lo));
        fStart = lo - margin;
        fStop = hi + margin;
    }
    const auto points = static_cast<std::size_t>(v.integer("points"));
    const double fMid = 0.5 * (fStart + fStop);
    const std::uint32_t q0 = sw.resonators.front().qubit;
    const OutputChain chain = env.outputChain(q0, fMid);
    const double attenuationDb = env.inputAttenuationDb(q0, fMid);
    const double baseline = std::pow(10.0, (chain.gainDb - attenuationDb) / 20.0); // a
    const double tau = env.electricalDelayS();
    const double chipWatts = wattsFromDbm(v.real("power") - attenuationDb);
    // b2 = S21 a1 + n: the fridge chain contributes E|n|² = k_B T_sys G · IFBW / averages, and the
    // receiver its own floor, (0 dBm − dynamic range − 10 dB) per Hz at the port: complex variance
    // of a point.
    const double averages = static_cast<double>(v.integer("averages"));
    const double receiverWPerHz = wattsFromDbm(-v.real("dynamic_range_db") - 10.0);
    const double variance =
        baseline * baseline * units::consts::k_B.v * chain.tSysK * v.real("if_bandwidth") /
            (averages * chipWatts) +
        receiverWPerHz * v.real("if_bandwidth") / (averages * wattsFromDbm(v.real("power")));
    sw.sigma = std::sqrt(variance / 2.0);
    sw.snrDb = 10.0 * std::log10(baseline * baseline / variance);

    const std::string stateSetting = v.text("qubit_state");
    struct Term {
        double fr0, fr1, p1, ql, qc, phi;
    };
    std::vector<Term> terms;
    const std::int64_t fitQubit = v.integer("fit_qubit");
    for (auto const& r : sw.resonators) {
        double qi = r.qInternal, ql = r.loadedQ(), photons = 0.0;
        for (int it = 0; it < 4;
             ++it) { // n̄ = 2 Q_l² P / (|Q_c| ħ ω_r²) on resonance; Q_i(n̄) when TLS is on
            photons = 2.0 * ql * ql * chipWatts /
                      (r.qCoupling * units::consts::hbar.v * std::pow(kTwoPi * r.frHz, 2));
            if (!v.flag("tls_model"))
                break;
            const double F = v.real("tls_factor");
            qi = r.qInternal * F /
                 (1.0 + (F - 1.0) / std::sqrt(1.0 + photons / v.real("tls_critical_photons")));
            ql = r.loadedQ(qi);
        }
        if (fitQubit < 0 ? &r == &sw.resonators.front()
                         : r.qubit == static_cast<std::uint32_t>(fitQubit))
            sw.photons = photons;
        double chi = r.chiHz;
        if (v.flag("punch_out") && r.criticalPhotons > 0.0)
            chi /= 1.0 + photons / r.criticalPhotons;
        // "1" excites the qubit under test (`fit_qubit`), or every qubit when none is selected.
        double p1 =
            stateSetting == "1" && (fitQubit < 0 || r.qubit == static_cast<std::uint32_t>(fitQubit))
                ? 1.0
                : 0.0;
        if (stateSetting == "live" && ctx.run && ctx.run->state &&
            r.qubit < ctx.run->state->nQubits)
            if (auto pops = levelPopulations(*ctx.run->state, r.qubit))
                p1 = pops->size() > 1 ? (*pops)[1] : 0.0;
        terms.push_back(
            {r.frHz - chi, r.frHz + chi, p1, ql, r.qCoupling, r.phi}); // T05 (6.2): ω_r ∓ χ
    }
    sw.f.resize(points);
    sw.s21.resize(points);
    for (std::size_t i = 0; i < points; ++i) {
        const double f =
            fStart + (fStop - fStart) * static_cast<double>(i) / static_cast<double>(points - 1);
        Complex notch{};
        for (auto const& t : terms)
            notch += (1.0 - t.p1) * notchTerm(f, t.fr0, t.ql, t.qc, t.phi) +
                     t.p1 * notchTerm(f, t.fr1, t.ql, t.qc, t.phi);
        const Complex clean =
            baseline * std::exp(kI * (kBaselinePhase - kTwoPi * f * tau)) * (1.0 - notch);
        sw.f[i] = f;
        sw.s21[i] = clean + Complex{ctx.rng.normal(0.0, sw.sigma), ctx.rng.normal(0.0, sw.sigma)};
    }
    return sw;
}

std::optional<double> Vna::query(std::string_view path) const {
    const SettingValues v = snapshot();
    std::optional<double> span, minS21Db;
    {
        std::lock_guard lk(fitMu_);
        span = lastSpanHz_;
        minS21Db = lastMinS21Db_;
    }
    // `auto_span` (the default) derives the range from the resonators, so the settings are only the
    // answer before the first sweep.
    if (path == "span")
        return span ? *span : v.real("f_stop") - v.real("f_start");
    if (path == "P")
        return v.real("power");
    if (path == "trace")
        return minS21Db; // depth of the deepest notch on screen, dB
    const auto fit = lastFit();
    if (path == "f_r")
        return fit ? std::optional(fit->frHz) : std::nullopt;
    if (path == "kappa")
        return fit ? std::optional(fit->kappaHz) : std::nullopt;
    if (path == "Q_i")
        return fit ? std::optional(fit->qi) : std::nullopt;
    return InstrumentBase::query(path);
}

Result<Trace> Vna::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    QXL_TRY_ASSIGN(Sweep sw, sweep(ctx));
    Trace t = makeTrace(channel, ctx);
    const std::size_t n = sw.f.size();
    std::vector<double> re(n), im(n);
    double minMag = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < n; ++i) {
        re[i] = sw.s21[i].real();
        im[i] = sw.s21[i].imag();
        minMag = std::min(minMag, std::abs(sw.s21[i]));
    }
    { // what `instr.vna.span` and `instr.vna.trace` report: the sweep that was actually taken
        std::lock_guard lk(fitMu_);
        lastSpanHz_ = sw.f.back() - sw.f.front();
        lastMinS21Db_ = 20.0 * std::log10(std::max(minMag, 1e-300));
    }
    t.sigma = Uncertainty{};
    if (channel.name == "circle") {
        t.x = re;
        t.y = im;
        t.sigma->y.assign(n, sw.sigma);
    } else if (channel.name == "s21_complex") {
        t.x = sw.f;
        t.y = re;
        t.y_im = im;
        t.sigma->y.assign(n, sw.sigma);
        t.sigma->yIm.assign(n, sw.sigma);
    } else {
        t.x = sw.f;
        t.y.resize(n);
        t.sigma->y.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double mag = std::max(std::abs(sw.s21[i]), 1e-300);
            if (channel.name == "s21") {
                t.y[i] = 20.0 * std::log10(mag);
                t.sigma->y[i] = 20.0 / std::numbers::ln10 * sw.sigma / mag;
            } else {
                t.y[i] = std::arg(sw.s21[i]) * 180.0 / std::numbers::pi;
                t.sigma->y[i] = sw.sigma / mag * 180.0 / std::numbers::pi;
            }
        }
    }
    t.markers.push_back({sw.f.front(), 0.0, "snr_db", sw.snrDb, 0.0, "dB"});
    t.markers.push_back({sw.f.front(), 0.0, "photons", sw.photons, 0.0, ""});
    if (!ctx.settings.flag("fit"))
        return t;

    std::optional<double> near;
    if (const std::int64_t q = ctx.settings.integer("fit_qubit"); q >= 0)
        for (auto const& r : sw.resonators)
            if (r.qubit == static_cast<std::uint32_t>(q))
                near = r.frHz;
    auto fit = fitResonatorNotch(sw.f, re, im, sw.sigma, near);
    if (!fit) { // a failed fit leaves the trace usable; the panel shows why
        t.markers.push_back(
            {sw.f.front(), 0.0, "fit_failed: " + fit.error().message, 0.0, 0.0, ""});
        return t;
    }
    {
        std::lock_guard lk(fitMu_);
        lastFit_ = *fit;
    }
    auto yAt = [&](double f) { // marker height on this channel's y axis
        const auto it = std::lower_bound(sw.f.begin(), sw.f.end(), f);
        const auto i = static_cast<std::size_t>(
            std::min<std::ptrdiff_t>(it - sw.f.begin(), static_cast<std::ptrdiff_t>(n) - 1));
        return channel.name == "circle" ? im[i] : t.y[i];
    };
    const double mx = channel.name == "circle" ? re[(fit->first + fit->last) / 2] : fit->frHz;
    const double my = yAt(fit->frHz);
    t.markers.push_back({mx, my, "f_r", fit->frHz, fit->sigmaFrHz, "Hz"});
    t.markers.push_back({mx, my, "Q_l", fit->ql, fit->sigmaQl, ""});
    t.markers.push_back({mx, my, "Q_c", fit->qc, fit->sigmaQc, ""});
    t.markers.push_back({mx, my, "phi", fit->phi, fit->sigmaPhi, "rad"});
    t.markers.push_back({mx, my, "Q_i", fit->qi, fit->sigmaQi, ""});
    t.markers.push_back(
        {mx, my, "kappa", fit->kappaHz, fit->kappaHz * fit->sigmaQl / fit->ql, "Hz"});
    t.markers.push_back({mx, my, "chi2_ndf", fit->chi2ndf, 0.0, ""});
    return t;
}

} // namespace qlab::instr
