#pragma once
// Spec 11 §3 / T08 §4 — material thermal data loaded from Assets/Lab/Materials/*.json.
#include "Core/Error.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::cryo {

struct SpecificHeatModel {
    std::string model = "debye";  // "debye" (lattice + electronic) or "table"
    double thetaD_K = 300.0;      // Debye temperature
    double gamma_mJ_molK2 = 0.0;  // electronic coefficient (0 for insulators)
    double molarMass_g = 60.0;
    std::vector<std::pair<double, double>> table; // (T, c J/kg/K) when model == "table"
};

struct Material {
    std::string id;
    std::string source;
    std::vector<std::pair<double, double>> kTable; // (T K, k W/m/K), ascending T
    SpecificHeatModel cp;
    double density_kg_m3 = 1000.0;
    double emissivity = 0.1;
    bool superconducting = false; // NbTi: electrons stop conducting below Tc
    double Tc_K = 0.0;
    mutable std::vector<double> cumGrid_, cumVal_; // cache for cumulativeIntegral

    // Thermal conductivity, interpolated linearly in log T (clamped at table ends).
    double k(double T_K) const;
    // Θ(Tc, Th) = ∫ k dT (W/m), numerical (composite midpoint in log T, ≥ 200 panels).
    double conductivityIntegral(double Tc_K, double Th_K) const;
    // Cumulative ∫_{1e-4 K}^{T} k dT on a cached log grid (built lazily, 4000 points).
    double cumulativeIntegral(double T_K) const;
    // Specific heat J/(kg K): Debye lattice term + γT electronic term.
    double specificHeat(double T_K) const;
    // Enthalpy per kg between two temperatures (J/kg), used for cooldown estimates.
    double enthalpy(double Tlo_K, double Thi_K) const;
};

class MaterialCatalog {
public:
    // Loads every *.json of kind "qlab.material" in dir (default: assetDir()/Lab/Materials).
    static Result<MaterialCatalog> load(const std::filesystem::path& dir = {});
    static Result<Material> parse(const std::string& text);
    const Material* find(std::string_view id) const;
    Result<const Material*> get(std::string_view id) const;
    std::vector<std::string> ids() const;
    void add(Material m) { mats_[m.id] = std::move(m); }
    std::size_t size() const { return mats_.size(); }
private:
    std::map<std::string, Material, std::less<>> mats_;
};

// Debye specific heat function (J/kg/K) for reference and tests.
double debyeSpecificHeat(double T_K, double thetaD_K, double molarMass_g, double gamma_mJ_molK2);

} // namespace qlab::cryo
