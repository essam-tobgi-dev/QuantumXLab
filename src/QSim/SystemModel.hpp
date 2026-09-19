#pragma once
// Spec 07 §5 — the time-domain model the Lindblad and Trajectories backends integrate.
// `hw::HamiltonianBuilder` (spec 09 §5) fills H0, drives and collapse; `pulse::Schedule`
// (spec 10 §4) supplies the envelopes. Nothing in this header knows about either module.
#include "QSim/Types.hpp"
#include <functional>
#include <string>
#include <vector>

namespace qlab::qsim {

// A drive term handled as H += Re[Ω(t)] · inPhase + Im[Ω(t)] · quadrature, with Hermitian operators
// supplied by the caller. `driveFromLadder` builds inPhase = a + a† and quadrature = i(a† − a),
// which are σx and σy on the qubit levels, so Ω(t) = (Ωx(t) + iΩy(t))/2 reproduces T05 (7.1) and
// T07 (1.2): a resonant Ω(t) = Ω/2 is H = (Ω/2)σx and gives P₁ = sin²(Ωt/2); Ω(t) = iΩ/2 rotates
// about +y.
struct DriveTerm {
    std::string channel; // e.g. "d[0]", "u[0,1]" — diagnostics and schedule binding
    Matrix inPhase;      // Hermitian operator multiplying Re Ω(t)   (full system dimension)
    Matrix quadrature;   // Hermitian operator multiplying Im Ω(t)   (full system dimension)
    // Complex envelope in angular-frequency units (rad/s). Time in seconds.
    std::function<Complex(double)> envelope;
};

// Collapse operator L_k with its own rate already folded in (L = √γ · op).
struct CollapseOp {
    std::string name; // "T1 q0", "Tphi q1", "leakage q0"
    Matrix op;        // full system dimension
};

// The complete model: H(t) = H0 + Σ_j (Re Ω_j(t) A_j + Im Ω_j(t) B_j), with collapse operators L_k.
// H0 is already in the rotating frame of each site's drive frequency (T07 §1), so the remaining
// time dependence is only the slow envelope.
struct SystemModel {
    std::vector<std::uint32_t> siteDims; // per-site dimension, little-endian (site 0 = LSB)
    Matrix h0;                           // Hermitian, D × D with D = Π siteDims, in rad/s (ħ = 1)
    std::vector<DriveTerm> drives;
    std::vector<CollapseOp> collapse;
    std::vector<double> frameFrequenciesHz; // per site, for reporting and phase bookkeeping
    double durationS = 0.0;                 // schedule length; 0 = caller drives the stepping

    std::size_t dimension() const {
        std::size_t d = 1;
        for (auto s : siteDims)
            d *= s;
        return d;
    }
    bool empty() const { return siteDims.empty(); }
};

// Ladder operators on a single site of dimension d, embedded into the full space of `siteDims`.
Matrix ladderAnnihilate(std::span<const std::uint32_t> siteDims, std::uint32_t site);
Matrix numberOperator(std::span<const std::uint32_t> siteDims, std::uint32_t site);
// Projector onto level `level` of `site` (population read-out).
Matrix levelProjector(std::span<const std::uint32_t> siteDims, std::uint32_t site,
                      std::uint32_t level);
// Builds the (A, B) quadrature pair for a drive that couples through the ladder operator of a site:
// A = a + a†, B = i(a† − a) (σx, σy on the qubit levels; see DriveTerm for the envelope
// convention).
std::pair<Matrix, Matrix> driveFromLadder(std::span<const std::uint32_t> siteDims,
                                          std::uint32_t site);

} // namespace qlab::qsim
