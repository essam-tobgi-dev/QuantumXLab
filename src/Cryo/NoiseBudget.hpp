#pragma once
// Spec 11 §6, T07 §9–§10 — thermal photon propagation along input lines and the output chain's
// system noise temperature (Friis).
#include "Core/Error.hpp"
#include "Cryo/Coax.hpp"
#include "Cryo/HeatLoads.hpp"
#include "Cryo/Stages.hpp"
#include "Cryo/Wiring.hpp"
#include <string>
#include <vector>

namespace qlab::cryo {

struct PhotonStep {
    Stage stage;
    std::string elementId;
    double A_dB = 0; // attenuation applied at this element (0 for clamps)
    double n_in = 0, n_out = 0;
    double emitted = 0; // (1 − 1/A) n_th(f, T_stage)
};

struct LineNoise {
    std::string lineId;
    double f_Hz = 5e9;
    double n_in = 0;       // photon number at the RT input
    double n_chip = 0;     // photon number delivered to the chip (input lines)
    double T_eff_K = 0;    // radiative temperature of n_chip
    double P1_thermal = 0; // qubit excited population implied by n_chip, n/(1+2n)
    double totalAttenuation_dB = 0;
    std::vector<PhotonStep> steps;
};

struct OutputChainNoise {
    std::string lineId;
    double T_sys_K = 0; // referred to the chip (Friis)
    double gainTotal_dB = 0;
    double n_backaction = 0;      // photons reaching the chip from the warm side through isolators
    double quantumEfficiency = 0; // η = (ħω/2k_B) / T_sys-ish per T07 §10 (phase-preserving)
    std::vector<std::pair<std::string, double>> contributions_K; // per element T_N/G_before
};

struct NoiseBudgetOptions {
    double f_Hz = 5e9;
    double n_in = -1;              // <0: thermal at 300 K (n_th(f, 293 K))
    bool includeCableLoss = false; // T07 §9 tables exclude cable loss; the inspector can enable it
};

class NoiseBudget {
  public:
    explicit NoiseBudget(const CoaxCatalog& coax) : coax_(coax) {}

    // Eq. (9.2) of T07 per element from RT to the chip, at the given stage temperatures.
    LineNoise inputLine(const WiringLine& line, const StageArray& T_K,
                        NoiseBudgetOptions opt = {}) const;

    // Friis cascade chip → RT for an output line; also the warm-side back-action photons.
    OutputChainNoise outputLine(const WiringLine& line, const StageArray& T_K,
                                double f_Hz = 5e9) const;

    // All lines of a wiring at once.
    std::vector<LineNoise> allInputs(const Wiring& w, const StageArray& T_K,
                                     NoiseBudgetOptions opt = {}) const;

  private:
    const CoaxCatalog& coax_;
};

// Bose–Einstein occupation (T07 (9.1)) exposed for other modules through the cryo API.
double thermalPhotons(double f_Hz, double T_K);
double effectiveTemperature(double f_Hz, double n);

} // namespace qlab::cryo
