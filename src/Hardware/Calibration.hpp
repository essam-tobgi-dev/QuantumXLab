#pragma once
// Spec 09 §3 — measured device parameters with uncertainties and provenance.
#include "Core/Error.hpp"
#include "Core/Random.hpp"
#include "Units/Units.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace qlab::hw {

// A calibration entry: value, 1σ uncertainty (same unit), and the experiment that produced it.
template <class T> struct Cal {
    T value{};
    T sigma{};
    std::string source;
    bool isDefault() const { return source == "default"; }
};

using Matrix2d =
    std::array<std::array<double, 2>, 2>; // row-stochastic M[i][j] = P(read j | prepared i)

struct QubitCal {
    Cal<units::Frequency> f01;           // Hz
    Cal<units::Frequency> anharmonicity; // Hz (negative for transmons)
    Cal<units::Time> t1, t2echo, t2star;
    Cal<double> thermalPopulation;
    Cal<double> gateError1q; // average infidelity r = 1 - F_avg
    Cal<units::Time> duration1q;
    Cal<double> coherentErrorFraction1q;
    Cal<double> resetError;
    Matrix2d readoutAssignment{{{1.0, 0.0}, {0.0, 1.0}}};
    Cal<units::Time> readoutDuration;
    Cal<double> readoutCrosstalkDephasing;
    std::optional<Cal<units::Frequency>> readoutFrequency; // transmons
    std::optional<Cal<units::Frequency>> readoutChi;       // χ (ordinary frequency)
    std::optional<Cal<units::Frequency>> readoutKappa;
    Cal<double> leakage1q;
    std::optional<Cal<double>> lambDicke; // ions

    double readoutFidelity() const {
        return 1.0 - 0.5 * (readoutAssignment[0][1] + readoutAssignment[1][0]);
    }
    // Pure dephasing time: 1/T_φ = 1/T2 − 1/(2 T1) (T04 §4).
    units::Time tphi() const;
    // Effective qubit temperature from the thermal population (spec 09 §3).
    units::Temperature effectiveTemperature() const;
};

struct EdgeCal {
    std::uint32_t a = 0, b = 0;
    std::string nativeGate; // cx | ecr | cz | siswap | ms (device default when empty)
    Cal<double> gateError2q;
    Cal<double> coherentErrorFraction2q;
    Cal<double> leakage2q;
    Cal<units::Frequency> zz; // Hz (ζ/2π)
    Cal<units::Time> duration;
    Cal<units::Frequency> couplingG; // g/2π
    std::optional<std::uint32_t> coupler;
    // Optional per-gate overrides on the same edge (e.g. siswap on a cz-native edge): gate →
    // (error, duration).
    std::map<std::string, std::pair<Cal<double>, Cal<units::Time>>> extraGates;
};

struct MotionalCal {
    std::vector<double> equilibriumPositions; // in units of ℓ
    std::vector<units::Frequency> axialModes;
    std::vector<units::Frequency> radialModes;
    // The mode that mediates the Mølmer–Sørensen gate (calibration `motional.gate_mode`). The
    // shipped 11-ion chain uses the axial COM, the 32-ion chain the radial COM (T06 §6).
    std::string gateModeAxis = "axial";        // "axial" | "radial"
    std::uint32_t gateModeIndex = 0;           // index into the list of that axis
    std::vector<double> gateModeParticipation; // b_n, normalised; empty → uniform
    std::optional<double> heatingQuantaPerS;   // calibrated heating rate of the gate mode

    // Frequency of the gate mode, or nullopt when the named list is too short.
    std::optional<units::Frequency> gateModeFrequency() const {
        const auto& list = gateModeAxis == "radial" ? radialModes : axialModes;
        if (gateModeIndex < list.size())
            return list[gateModeIndex];
        return std::nullopt;
    }
};

struct Calibration {
    std::string device;
    std::string timestamp;
    std::vector<QubitCal> qubits; // indexed by qubit index (couplers may be present with defaults)
    std::map<std::string, EdgeCal> edges; // key "a-b" as in the file (a < b for undirected)
    std::optional<MotionalCal> motional;

    static std::string edgeKey(std::uint32_t a, std::uint32_t b);
    const EdgeCal* edge(std::uint32_t a, std::uint32_t b) const; // either order
    const QubitCal* qubit(std::uint32_t q) const {
        return q < qubits.size() ? &qubits[q] : nullptr;
    }

    // Gate duration/error lookups used by the scheduler and noise model (spec 14 §8, spec 08 §4).
    // `gate` is a native gate name; for 1-qubit gates only qubits[0] is used; "rz"/"id" are 0 ns.
    Result<units::Time> gateDuration(std::string_view gate,
                                     std::span<const std::uint32_t> qubits) const;
    Result<double> gateError(std::string_view gate, std::span<const std::uint32_t> qubits) const;
    struct IdleParams {
        units::Time t1, t2, t2star, tphi;
        double pThermal;
    };
    Result<IdleParams> idleParams(std::uint32_t q) const;
    units::Time maxT1() const;

    // Validation rules of spec 09 §3 (T2* ≤ T2echo ≤ 2T1, row-stochastic readout, ranges). Collects
    // all violations.
    Result<void> validate() const;

    // Deterministic perturbation with the spec 09 §8 spreads (used by tests and the drift
    // tutorial).
    struct Spreads {
        double t1Ln = 0.30, t2Ln = 0.35, errLn = 0.25, fSigmaHz = 15e6, alphaSigmaHz = 6e6,
               readoutRel = 0.3, pThermalLn = 0.3;
    };
    Calibration perturbed(std::uint64_t seed, Spreads s) const;
    Calibration perturbed(std::uint64_t seed) const { return perturbed(seed, Spreads{}); }
};

} // namespace qlab::hw
