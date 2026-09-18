#pragma once
// Spec 22 §5 — two-component 2D Gaussian mixture (IQ readout blobs) fitted by EM; LDA-style
// linear discriminant and assignment fidelity from the fitted components (spec 12 §4).
#include "Core/Error.hpp"
#include <array>
#include <span>
#include <vector>

namespace qlab::data::fit {

struct Gaussian2 {
    double mx = 0, my = 0;           // mean
    double sxx = 1, sxy = 0, syy = 1; // covariance
    double weight = 0.5;
    double pdf(double x, double y) const;
};

struct Gmm2Result {
    std::array<Gaussian2, 2> comp;    // comp[0] ≈ |0⟩ cluster (smaller I by convention after sort)
    double logLikelihood = 0;
    int iterations = 0;
    bool converged = false;
    // Linear discriminant w·(x,y) + b > 0 → component 1 (Fisher LDA on the fitted components).
    double wx = 1, wy = 0, b = 0;
    // Separation |μ0-μ1| / σ along the discriminant and the corresponding assignment error
    // P(err) = ½ erfc(SNR / (2√2)) with SNR = |μ0-μ1|/σ (T07 §7).
    double snr = 0;
    double assignmentError = 0;
    int classify(double x, double y) const { return wx * x + wy * y + b > 0 ? 1 : 0; }
};

struct Gmm2Options {
    int maxIterations = 200;
    double tol = 1e-8;
    bool sharedCovariance = true;  // default per spec 22 §5 (readout blobs share noise)
    std::span<const int> labels;   // optional known labels (prepared state) for initialization
};

Result<Gmm2Result> fitGmm2(std::span<const double> x, std::span<const double> y, const Gmm2Options& opt = {});

// Assignment matrix M_ij = P(read j | prepared i) from labelled IQ points and a discriminant.
std::array<std::array<double, 2>, 2> assignmentMatrix(std::span<const double> x, std::span<const double> y,
                                                       std::span<const int> prepared, const Gmm2Result& g);

} // namespace qlab::data::fit
