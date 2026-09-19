#pragma once
// Spec 09 §5.5 — assembly of the multi-qubit time-domain model that the Lindblad and
// Trajectories backends integrate (spec 07 §5). Built from a Device + Calibration; the
// per-site rotating frame of T07 §1 is applied here so that only slow envelopes remain
// time-dependent. Hardware does not depend on QSim: this is a plain struct that
// `noise`/`pulse` copy field-by-field into `qsim::SystemModel`.
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include "Numerics/Numerics.hpp"
#include <span>
#include <string>
#include <vector>

namespace qlab::hw {

// A drive term: H += Re[Ω(t)]·inPhase + Im[Ω(t)]·quadrature, Ω in rad/s (T07 §2).
struct DriveSpec {
    std::string channel;                 // "d[0]", "u[0,1]", "f[2]", "ms[0,1]"
    std::uint32_t site = 0;              // site the channel drives (control qubit for a CR channel)
    std::optional<std::uint32_t> target; // CR target, when the channel is a u[c,t]
    num::SparseMatrix inPhase;           // A = a + a†   (rad/s per unit Ω)
    num::SparseMatrix quadrature;        // B = i(a − a†)
    double frameFrequencyHz = 0.0;       // frequency the frame of this channel rotates at
};

// Collapse operator with its rate folded in: L = √γ · op (units of √(1/s)).
struct CollapseSpec {
    std::string name; // "T1 q0", "Tphi q1", "leakage q0", "heating"
    num::SparseMatrix op;
};

// H(t) = H0 + Σ_j (Re Ω_j(t)·A_j + Im Ω_j(t)·B_j), with collapse operators L_k.
struct SystemModelSpec {
    std::vector<std::uint32_t> siteDims; // little-endian: site 0 is the least significant index
    num::SparseMatrix h0;                // Hermitian, rad/s (ħ = 1), in the rotating frame
    std::vector<DriveSpec> drives;
    std::vector<CollapseSpec> collapse;
    std::vector<double> frameFrequenciesHz;   // per site
    std::vector<std::uint32_t> qubitIndices;  // device qubit index of each site
    std::string frame = "per_qubit_rotating"; // or "lab"
    bool rwa = true;

    std::size_t dimension() const;
    // Dense views for the backend adapter (spec 07 §5 takes dense matrices).
    num::Matrix h0Dense() const { return h0.toDense(); }
};

// What to include when assembling. Collapse operators come from the calibration
// (T1 → √γ₁ σ⁻ with thermal partner, Tφ → √(γφ/2) σ_z, leakage → √γ_L |2⟩⟨1|).
struct SystemModelOptions {
    std::span<const std::uint32_t> qubits; // device qubits to include, in site order
    std::uint32_t levels = 3;              // transmon Duffing truncation (spec 07 §5: d = 3)
    bool includeCoupling = true;           // static exchange between included pairs
    bool includeDecoherence = true;
    bool includeThermal = true;             // upward jump at rate γ n_th
    bool includeLeakage = false;            // |1⟩→|2⟩ collapse channel
    bool rwa = true;                        // drop counter-rotating terms
    bool labFrame = false;                  // keep site frequencies in H0
    std::optional<double> frameFrequencyHz; // shared frame (default: each site at its own f01)
    int fockCutoff = 8;                     // ions: motional Fock cutoff
};

// Transmon devices: Duffing sites with exchange coupling (T05 §3.4, §5.1).
Result<SystemModelSpec> buildTransmonModel(const Device& dev, const Calibration& cal,
                                           const SystemModelOptions& opt);
// Ion devices: two-level qubits ⊗ one shared motional mode (T06 §3, §6).
Result<SystemModelSpec> buildIonModel(const Device& dev, const Calibration& cal,
                                      const SystemModelOptions& opt);
// Dispatches on dev.technology; enforces the dimension cap of spec 07 §5 (3^5 transmon sites).
Result<SystemModelSpec> buildSystemModel(const Device& dev, const Calibration& cal,
                                         const SystemModelOptions& opt);

// Operators on the assembled site space (little-endian embedding).
num::SparseMatrix siteAnnihilate(std::span<const std::uint32_t> dims, std::uint32_t site);
num::SparseMatrix siteNumber(std::span<const std::uint32_t> dims, std::uint32_t site);
num::SparseMatrix siteProjector(std::span<const std::uint32_t> dims, std::uint32_t site,
                                std::uint32_t level);
num::SparseMatrix siteOperator(std::span<const std::uint32_t> dims, std::uint32_t site,
                               num::ConstMatrixView local);

} // namespace qlab::hw
