#pragma once
// Spec 21 §3.5–3.6 — presentation of a density matrix: city-plot bars (height = Re, Im or |ρ_ij|,
// colour = sign or phase hue) and Hinton squares (area ∝ |ρ_ij|, colour = phase hue). Both are
// limited to 8 qubits (256² elements); above that the view shows a reduced ρ of a qubit subset.
#include "Core/Error.hpp"
#include "Numerics/Types.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace qlab::viz::math {

inline constexpr std::uint32_t kDensityPlotMaxQubits = 8;

enum class CityQuantity : std::uint8_t {
    Real,       // height Re ρ_ij, diverging colour by sign
    Imag,       // height Im ρ_ij, diverging colour by sign
    Magnitude,  // height |ρ_ij|, sequential colour
    Phase       // height |ρ_ij|, colour = phase hue of arg ρ_ij (spec 21 §2.4)
};

struct CityBar {
    std::uint32_t row = 0, col = 0;   // ρ_ij = ⟨i|ρ|j⟩, little-endian basis indices
    num::Complex value{};
    double height = 0.0;              // signed for Real/Imag; the plot's vertical scale is ±maxAbs
    glm::vec3 color{0.0f};            // display colour
    bool diagonal = false;            // highlighted (spec 21 §3.5)
};

struct CityModel {
    std::size_t dim = 0;
    std::uint32_t nQubits = 0;
    CityQuantity quantity = CityQuantity::Real;
    std::vector<CityBar> bars;        // row-major; elements with |height| ≤ cutoff·maxAbs are omitted
    double maxAbs = 0.0;              // max_ij |ρ_ij|: the legend range
};
// `rho` must be 2^n × 2^n with n ≤ 8. `relativeCutoff` drops bars too small to see (default: keep
// everything above 1e-6 of the largest element).
Result<CityModel> buildCity(const num::Matrix& rho, CityQuantity quantity = CityQuantity::Real,
                            double relativeCutoff = 1e-6);

struct HintonSquare {
    std::uint32_t row = 0, col = 0;
    num::Complex value{};
    double side = 0.0;                // fraction of the cell edge: √(|ρ_ij| / maxAbs), so area ∝ |ρ_ij|
    glm::vec3 color{0.0f};            // phase hue
};

struct HintonModel {
    std::size_t dim = 0;
    std::uint32_t nQubits = 0;
    std::vector<HintonSquare> squares; // row-major, elements with |ρ_ij| ≤ cutoff·maxAbs omitted
    double maxAbs = 0.0;
};
Result<HintonModel> buildHinton(const num::Matrix& rho, double relativeCutoff = 1e-6);

} // namespace qlab::viz::math
