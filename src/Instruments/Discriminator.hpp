#pragma once
// Spec 12 §5, T10 §7.3 — state discrimination of integrated IQ points. Training fits a Gaussian
// mixture with means μ_s and one shared covariance (two states: data::fit::fitGmm2; three states,
// `states: 3`, an EM of the same form); the classifier is the linear boundary equidistant in the
// Mahalanobis metric (the perpendicular bisector after whitening). The assignment-matrix estimate
// is M_ij = #{prepared i, assigned j} / #{prepared i}, row-stochastic like noise::ReadoutModel, with
// the Wilson interval as σ. Class Statistical.
#include "Instruments/Types.hpp"
#include <array>
#include <span>
#include <vector>

namespace qlab::instr {

struct IqPoint {
    double i = 0.0, q = 0.0;   // volts at the ADC
    std::int8_t prepared = -1; // true state of the shot when known, else −1
    std::int8_t assigned = -1; // discriminator output, −1 before training
};

struct Discriminator {
    int states = 2;
    std::vector<std::array<double, 2>> means; // μ_s
    double sxx = 1.0, sxy = 0.0, syy = 1.0;   // shared covariance Σ
    double snr = 0.0;                         // |μ_1 − μ_0|/σ: Mahalanobis distance of states 0 and 1
    int iterations = 0;
    bool converged = false;

    // argmin_s (x − μ_s)ᵀ Σ⁻¹ (x − μ_s)
    int classify(double i, double q) const;
    // Signed coordinate along the 0→1 discriminating axis in units of σ, 0 on the boundary:
    // wᵀ(x − (μ_0 + μ_1)/2) / √(wᵀΣw) with w = Σ⁻¹(μ_1 − μ_0). The histogram channel bins this.
    double project(double i, double q) const;
};

struct AssignmentEstimate {
    int states = 2;
    std::vector<std::vector<double>> matrix; // M_ij = P(assigned j | prepared i)
    std::vector<std::vector<double>> sigma;  // Wilson half-width at z = 1
    std::vector<std::uint64_t> shots;        // prepared-i shot counts
    // 1 − (M_01 + M_10)/2 (spec 08 §3).
    double fidelity() const;
};

// Fits the mixture to labelled calibration shots (every point needs `prepared` in [0, states)).
// The labels seed the EM and decide which component is which state; the fit itself is unsupervised,
// so shots that decayed during the window do not drag the means. Errors: BadInput (a state without
// shots, a label out of range), FitFailed.
Result<Discriminator> trainDiscriminator(std::span<const IqPoint> labelled, int states = 2);
// Classifies every labelled point and counts. Points without a label are skipped.
AssignmentEstimate estimateAssignment(std::span<const IqPoint> labelled, const Discriminator& d);

} // namespace qlab::instr
