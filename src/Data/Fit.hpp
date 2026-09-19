#pragma once
// Spec 22 §5 — Levenberg–Marquardt fitting engine with bounds by transformation.
#include "Core/Error.hpp"
#include "Data/Fidelity.hpp"
#include <complex>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qlab::data::fit {

using Complex = std::complex<double>;
constexpr double kInf = std::numeric_limits<double>::infinity();

struct Bound {
    double lo = -kInf, hi = kInf;
};

struct ParamInfo {
    std::string name;
    std::string unit;
    Bound bound;
};

// A parametric model. Real models implement eval/jacobian; complex ones (resonator S21)
// implement evalC/jacobianC and set complexValued = true; the engine then fits Re and Im as
// 2N residuals (spec 22 §5.1).
class FitModel {
  public:
    virtual ~FitModel() = default;
    virtual std::string id() const = 0;
    virtual std::string latex() const = 0;
    virtual std::vector<ParamInfo> params() const = 0;
    virtual bool complexValued() const { return false; }
    virtual double eval(std::span<const double> beta, double x) const {
        (void)beta;
        (void)x;
        return 0.0;
    }
    virtual Complex evalC(std::span<const double> beta, double x) const {
        return {eval(beta, x), 0.0};
    }
    // Analytic Jacobian d f / d beta_j; return false to fall back to central differences.
    virtual bool jacobian(std::span<const double> beta, double x, std::span<double> out) const {
        (void)beta;
        (void)x;
        (void)out;
        return false;
    }
    virtual bool jacobianC(std::span<const double> beta, double x, std::span<Complex> out) const {
        (void)beta;
        (void)x;
        (void)out;
        return false;
    }
    virtual std::vector<double> initialGuess(std::span<const double> x, std::span<const double> y,
                                             std::span<const double> yIm = {}) const = 0;
    // Derived quantities reported alongside parameters (e.g. Q_i, a_pi, r).
    virtual std::vector<std::pair<std::string, double>>
    derived(std::span<const double> beta) const {
        (void)beta;
        return {};
    }
    std::size_t nparams() const { return params().size(); }
};

struct FitOptions {
    int maxIterations = 200;
    double lambda0 = 1e-3;
    double lambdaUp = 10.0;
    double lambdaDown = 3.0;
    double chi2RelTol = 1e-10;
    double stepRelTol = 1e-8;
    std::optional<std::vector<double>> initial; // overrides model.initialGuess
    std::optional<std::vector<Bound>> bounds;   // overrides model param bounds
    std::vector<bool> fixed;                    // parameters held at their initial value
};

struct FitResult {
    std::vector<double> beta, sigma;
    std::vector<std::vector<double>> covariance; // nparams x nparams
    double chi2 = 0, ndf = 0, chi2ndf = 0, r2 = 0;
    std::vector<double> residuals; // weighted residuals (2N for complex)
    bool converged = false;
    int iterations = 0;
    std::string message;
    std::vector<std::string> paramNames;
    std::vector<std::pair<std::string, double>> derived;
    std::vector<bool> atBound;
    FidelityClass cls = FidelityClass::Statistical;
    double param(std::string_view name) const;
    double error(std::string_view name) const;
    // |rho_jk| > 0.95 flagged as degenerate (spec 22 §5.2).
    std::vector<std::pair<std::size_t, std::size_t>> degeneratePairs(double threshold = 0.95) const;
};

// Fit y(x) (and yIm for complex models) with optional per-point sigma (weights).
Result<FitResult> fitModel(const FitModel& model, std::span<const double> x,
                           std::span<const double> y, std::span<const double> sigma = {},
                           const FitOptions& opt = {}, std::span<const double> yIm = {});

// Small dense linear algebra used by the engine (also exposed for tests).
namespace linalg {
// Solve A x = b for symmetric positive-definite A (Cholesky). Returns false if not SPD.
bool solveSpd(std::vector<std::vector<double>> A, std::vector<double> b, std::vector<double>& x);
bool invertSpd(const std::vector<std::vector<double>>& A, std::vector<std::vector<double>>& inv);
} // namespace linalg

// Frequency guess by zero-padded DFT peak (excluding DC), for uniformly or non-uniformly spaced x.
double dominantFrequency(std::span<const double> x, std::span<const double> y,
                         double* phase = nullptr, double* amplitude = nullptr);

} // namespace qlab::data::fit
