#pragma once
// Spec 12 §6 — vector network analyzer: two-port S21 sweep of a readout feedline through the full
// input and output chain. Every resonator on the feedline contributes a notch (T05 §6.3),
//   S21(f) = a e^{iθ} e^{−2πifτ} [ 1 − Σ_r (Q_l/|Q_c|) e^{iφ} / (1 + 2iQ_l (f − f_r)/f_r) ],
//   1/Q_l = 1/Q_i + cos φ/|Q_c|,   κ = 2π f_r / Q_l,
// with f_r − χ for the qubit in |0⟩ and f_r + χ in |1⟩ (T05 (6.2)); in `live` mode the trace is the
// population-weighted mixture p_0 S21⁽⁰⁾ + p_1 S21⁽¹⁾ of the run snapshot. a is the net gain of
// the two lines, τ the electrical delay of the cables, and the trace noise follows from
// k_B T_sys · IFBW / averages against the power reaching the chip. Options (Model): TLS loss
// saturation Q_i(n̄) and the punch-out of χ above n_crit. The built-in fit is the
// diameter-correction circle fit refined by data::fit's `resonator_notch` model (spec 22 §5).
#include "Instruments/InstrumentBase.hpp"
#include <span>

namespace qlab::instr {

struct NotchResonator {
    std::uint32_t qubit = 0;
    double frHz = 7.0e9;             // bare resonator frequency
    double qInternal = 5e5;          // Q_i at single-photon power
    double qCoupling = 1e4;          // |Q_c|
    double phi = 0.0;                // impedance-mismatch angle φ
    double chiHz = 0.0;              // χ/2π, signed
    double criticalPhotons = 0.0;    // n_crit (T05 (6.4)); 0 = unknown, no punch-out
    double loadedQ(double qi) const; // 1/Q_l = 1/Q_i + cos φ/|Q_c|
    double loadedQ() const { return loadedQ(qInternal); }
};

// Notch factor of one resonator at frequency f (the bracketed term's subtrahend).
Complex notchTerm(double fHz, double frHz, double ql, double qc, double phi);

struct ResonatorFit {
    double frHz = 0, ql = 0, qc = 0, phi = 0, qi = 0, kappaHz = 0; // κ/2π = f_r/Q_l
    double amplitude = 0, alpha = 0, tauS = 0; // environment a e^{iα} e^{−2πifτ}
    double sigmaFrHz = 0, sigmaQl = 0, sigmaQc = 0, sigmaPhi = 0, sigmaQi = 0;
    double chi2ndf = 0;
    bool converged = false;
    std::size_t first = 0, last = 0; // window of the trace that was fitted
    FidelityClass cls = FidelityClass::Statistical;
};

// Fit of one notch in a complex trace. `nearHz` picks the dip closest to a frequency (the deepest
// one when absent); `sigma` is the per-quadrature noise of the points (0: unknown, the residuals
// set the scale). Errors: FitFailed (no dip, too few points, engine failure).
Result<ResonatorFit> fitResonatorNotch(std::span<const double> fHz, std::span<const double> re,
                                       std::span<const double> im, double sigma = 0.0,
                                       std::optional<double> nearHz = std::nullopt);

class Vna final : public InstrumentBase {
  public:
    explicit Vna(std::uint32_t index = 0);
    static SettingSchema makeSchema();

    // Resonators of the swept feedline: the override when set (test benches, synthetic devices),
    // else from the Environment's device and calibration (Q_l = f_r/κ, Q_i from `q_internal`).
    void setResonators(std::optional<std::vector<NotchResonator>> resonators);
    Result<std::vector<NotchResonator>> resonators() const;
    // Last built-in fit (acquire with `fit` on); written into calibration.json by the
    // recalibration workflow: readout_f_ghz = frHz, readout_kappa_mhz = kappaHz.
    std::optional<ResonatorFit> lastFit() const;
    // span (of the sweep actually taken), P, trace (min |S21| of the last sweep, dB), f_r, kappa,
    // Q_i.
    std::optional<double> query(std::string_view path) const override;

  protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
    std::string conflict(const SettingValues& values) const override;
    bool triggerSourceSet(const SettingValues&) const override { return false; }

  private:
    struct Sweep {
        std::vector<double> f;
        std::vector<Complex> s21;
        double sigma = 0.0;   // per-quadrature noise of a point
        double snrDb = 0.0;   // baseline |S21|² over the complex noise variance
        double photons = 0.0; // n̄ in the fitted resonator at this power
        std::vector<NotchResonator> resonators;
    };
    Result<Sweep> sweep(AcquireContext& ctx) const;

    mutable std::mutex fitMu_;
    std::optional<std::vector<NotchResonator>> override_;
    std::optional<ResonatorFit> lastFit_;
    // The sweep actually taken, for the `span` and `trace` binding paths: with `auto_span` on (the
    // default) the swept range is derived from the resonators, not from f_start/f_stop.
    std::optional<double> lastSpanHz_;
    std::optional<double> lastMinS21Db_;
};

} // namespace qlab::instr
