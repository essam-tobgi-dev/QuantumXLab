#pragma once
// Spec 22 §5 — fit model catalogue. Each model: id, LaTeX, parameters with bounds, analytic
// Jacobian, initial-guess heuristic, and derived quantities. Ids are the ones spec 22 §5 lists.
#include "Data/Fit.hpp"
#include <memory>
#include <string_view>

namespace qlab::data::fit {

// Factory: nullptr if `id` is unknown. Known ids: exp_decay, ramsey, echo, echo_gauss,
// rabi_amp, rabi_time, lorentzian, gaussian, resonator_notch, rb_decay, rb_interleaved, linear.
std::unique_ptr<FitModel> makeModel(std::string_view id);
std::vector<std::string> modelIds();

// A e^{-t/T} + c
class ExpDecayModel final : public FitModel {
  public:
    std::string id() const override { return "exp_decay"; }
    std::string latex() const override { return "A e^{-t/T_1} + c"; }
    std::vector<ParamInfo> params() const override;
    double eval(std::span<const double> b, double x) const override;
    bool jacobian(std::span<const double> b, double x, std::span<double> out) const override;
    std::vector<double> initialGuess(std::span<const double> x, std::span<const double> y,
                                     std::span<const double>) const override;
};

// A e^{-t/T} cos(2π δ t + φ) + c   (Ramsey, T = T2*)
class RamseyModel final : public FitModel {
  public:
    std::string id() const override { return "ramsey"; }
    std::string latex() const override { return "A e^{-t/T_2^*}\\cos(2\\pi\\delta t + \\phi) + c"; }
    std::vector<ParamInfo> params() const override;
    double eval(std::span<const double> b, double x) const override;
    bool jacobian(std::span<const double> b, double x, std::span<double> out) const override;
    std::vector<double> initialGuess(std::span<const double> x, std::span<const double> y,
                                     std::span<const double>) const override;
};

// A e^{-(t/T)^n} + c with n fixed at construction (1 = exponential echo, 2 = Gaussian echo)
class EchoModel final : public FitModel {
  public:
    explicit EchoModel(double n = 1.0) : n_(n) {}
    std::string id() const override { return n_ == 2.0 ? "echo_gauss" : "echo"; }
    std::string latex() const override {
        return n_ == 2.0 ? "A e^{-(t/T_{2E})^2} + c" : "A e^{-t/T_{2E}} + c";
    }
    std::vector<ParamInfo> params() const override;
    double eval(std::span<const double> b, double x) const override;
    bool jacobian(std::span<const double> b, double x, std::span<double> out) const override;
    std::vector<double> initialGuess(std::span<const double> x, std::span<const double> y,
                                     std::span<const double>) const override;
    double exponent() const { return n_; }

  private:
    double n_;
};

// A cos(π a / a_π) + c   (amplitude Rabi; a_π is the π-pulse amplitude)
class RabiAmpModel final : public FitModel {
  public:
    std::string id() const override { return "rabi_amp"; }
    std::string latex() const override { return "A\\cos(\\pi a/a_\\pi) + c"; }
    std::vector<ParamInfo> params() const override;
    double eval(std::span<const double> b, double x) const override;
    bool jacobian(std::span<const double> b, double x, std::span<double> out) const override;
    std::vector<double> initialGuess(std::span<const double> x, std::span<const double> y,
                                     std::span<const double>) const override;
    std::vector<std::pair<std::string, double>> derived(std::span<const double> b) const override;
};

// A cos(2π f t + φ) e^{-t/τ} + c   (time Rabi)
class RabiTimeModel final : public FitModel {
  public:
    std::string id() const override { return "rabi_time"; }
    std::string latex() const override { return "A\\cos(2\\pi f t + \\phi)e^{-t/\\tau} + c"; }
    std::vector<ParamInfo> params() const override;
    double eval(std::span<const double> b, double x) const override;
    bool jacobian(std::span<const double> b, double x, std::span<double> out) const override;
    std::vector<double> initialGuess(std::span<const double> x, std::span<const double> y,
                                     std::span<const double>) const override;
};

// A (Γ/2)^2 / ((x-x0)^2 + (Γ/2)^2) + c   (Γ = FWHM)
class LorentzianModel final : public FitModel {
  public:
    std::string id() const override { return "lorentzian"; }
    std::string latex() const override {
        return "A\\frac{(\\Gamma/2)^2}{(x-x_0)^2+(\\Gamma/2)^2} + c";
    }
    std::vector<ParamInfo> params() const override;
    double eval(std::span<const double> b, double x) const override;
    bool jacobian(std::span<const double> b, double x, std::span<double> out) const override;
    std::vector<double> initialGuess(std::span<const double> x, std::span<const double> y,
                                     std::span<const double>) const override;
};

// A exp(-(x-x0)^2 / (2σ^2)) + c
class GaussianModel final : public FitModel {
  public:
    std::string id() const override { return "gaussian"; }
    std::string latex() const override { return "A e^{-(x-x_0)^2/2\\sigma^2} + c"; }
    std::vector<ParamInfo> params() const override;
    double eval(std::span<const double> b, double x) const override;
    bool jacobian(std::span<const double> b, double x, std::span<double> out) const override;
    std::vector<double> initialGuess(std::span<const double> x, std::span<const double> y,
                                     std::span<const double>) const override;
};

// A p^m + B  (randomized benchmarking); derived: r = (1-p)(d-1)/d for d = 2^nq
class RbDecayModel final : public FitModel {
  public:
    explicit RbDecayModel(int nqubits = 1, bool interleaved = false)
        : nq_(nqubits), il_(interleaved) {}
    std::string id() const override { return il_ ? "rb_interleaved" : "rb_decay"; }
    std::string latex() const override { return "A p^m + B"; }
    std::vector<ParamInfo> params() const override;
    double eval(std::span<const double> b, double x) const override;
    bool jacobian(std::span<const double> b, double x, std::span<double> out) const override;
    std::vector<double> initialGuess(std::span<const double> x, std::span<const double> y,
                                     std::span<const double>) const override;
    std::vector<std::pair<std::string, double>> derived(std::span<const double> b) const override;
    // Reference decay for interleaved RB: r_gate = (1 - p_int/p_ref)(d-1)/d
    void setReference(double pRef) { pRef_ = pRef; }

  private:
    int nq_;
    bool il_;
    double pRef_ = 1.0;
};

// a + b x
class LinearModel final : public FitModel {
  public:
    std::string id() const override { return "linear"; }
    std::string latex() const override { return "a + b x"; }
    std::vector<ParamInfo> params() const override;
    double eval(std::span<const double> b, double x) const override;
    bool jacobian(std::span<const double> b, double x, std::span<double> out) const override;
    std::vector<double> initialGuess(std::span<const double> x, std::span<const double> y,
                                     std::span<const double>) const override;
};

// Notch resonator (spec 12 §5, 22 §5): complex-valued
//   S21(f) = a e^{iα} e^{-2π i f τ} [ 1 - (Q_l/|Q_c|) e^{iφ} / (1 + 2i Q_l (f-f_r)/f_r) ]
// Parameters: f_r, Q_l, Q_c (|Q_c|), phi, a, alpha, tau.  Derived: Q_i = 1/(1/Q_l - Re(1/Q_c
// e^{-iφ})).
class ResonatorNotchModel final : public FitModel {
  public:
    std::string id() const override { return "resonator_notch"; }
    std::string latex() const override {
        return "S_{21}(f) = a e^{i\\alpha} e^{-2\\pi i f\\tau}\\left[1 - "
               "\\frac{(Q_l/|Q_c|)e^{i\\phi}}{1 + 2iQ_l(f-f_r)/f_r}\\right]";
    }
    std::vector<ParamInfo> params() const override;
    bool complexValued() const override { return true; }
    Complex evalC(std::span<const double> b, double x) const override;
    bool jacobianC(std::span<const double> b, double x, std::span<Complex> out) const override;
    std::vector<double> initialGuess(std::span<const double> x, std::span<const double> y,
                                     std::span<const double> yIm) const override;
    std::vector<std::pair<std::string, double>> derived(std::span<const double> b) const override;
    static double internalQ(double Ql, double Qc, double phi);
};

// RB helper (spec 22 §5): average error per Clifford from decay p.
inline double rbErrorFromP(double p, int nqubits) {
    double d = static_cast<double>(1u << nqubits);
    return (1.0 - p) * (d - 1.0) / d;
}

} // namespace qlab::data::fit
