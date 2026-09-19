#pragma once
// Spec 11 §2.1 / T08 §4.1 — coaxial and DC-loom cable catalog and conduction heat loads.
#include "Core/Error.hpp"
#include "Cryo/Materials.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace qlab::cryo {

struct CoaxSpec {
    std::string id; // SS_086, CuNi_086, NbTi_086, Cu_141, SS_219, Cu_086, DC_loom_12
    double od_mm = 2.19;
    double areaInner_m2 = 0; // centre conductor cross-section
    double areaDielectric_m2 = 0;
    double areaOuter_m2 = 0;   // outer conductor cross-section
    std::string innerMaterial; // material ids from the MaterialCatalog
    std::string dielectricMaterial = "PTFE";
    std::string outerMaterial;
    double loss_dB_per_m_5GHz = 0; // at 300 K (Model)
    int conductors = 1;            // 24 for a 12-pair loom (areas are per conductor)
    bool superconductingOuter = false;
};

struct ConductionLoad {
    double inner_W = 0, dielectric_W = 0, outer_W = 0;
    double total() const { return inner_W + dielectric_W + outer_W; }
};

class CoaxCatalog {
  public:
    CoaxCatalog(); // populated with the standard entries of spec 11 §2.1 and T08 §4.1
    const CoaxSpec* find(std::string_view id) const;
    Result<const CoaxSpec*> get(std::string_view id) const;
    std::vector<std::string> ids() const;
    void add(CoaxSpec s);

  private:
    std::vector<CoaxSpec> specs_;
};

// Q = (A/L) Θ(Tc, Th) per part (spec 11 §2.1). Length in metres.
Result<ConductionLoad> conductionLoad(const CoaxSpec& coax, double length_m, double Tcold_K,
                                      double Thot_K, const MaterialCatalog& mats);

// Attenuation of a coax run at f (dB), scaled from the 5 GHz/300 K figure by sqrt(f/5GHz) and
// a 0.6 factor below 4 K for normal metals (Model); 0.02 dB/m for superconducting NbTi.
double coaxLoss_dB(const CoaxSpec& coax, double length_m, double f_Hz, double T_K);

} // namespace qlab::cryo
