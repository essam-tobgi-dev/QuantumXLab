// Spec 21 §3.5–3.6 — city-plot bars and Hinton squares (see DensityPlot.hpp).
#include "Viz/Math/DensityPlot.hpp"
#include "Viz/Math/Color.hpp"
#include "Viz/Math/Phase.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace qlab::viz::math {
namespace {

Result<std::uint32_t> checkedQubits(const num::Matrix& rho, const char* who) {
    if (rho.rows == 0 || rho.rows != rho.cols || !std::has_single_bit(rho.rows))
        return fail(ErrorCode::InvalidArgument,
                    std::string(who) + ": the density matrix must be 2^n x 2^n");
    const auto n = static_cast<std::uint32_t>(std::countr_zero(rho.rows));
    if (n > kDensityPlotMaxQubits)
        return fail(ErrorCode::OutOfRange,
                    std::string(who) + ": at most 8 qubits; pick a qubit subset (spec 21 §3.5)");
    return n;
}

double largestElement(const num::Matrix& rho) {
    double m = 0.0;
    for (const auto& v : rho.data)
        m = std::max(m, std::abs(v));
    return m;
}

} // namespace

Result<CityModel> buildCity(const num::Matrix& rho, CityQuantity quantity, double relativeCutoff) {
    QXL_TRY_ASSIGN(const std::uint32_t n, checkedQubits(rho, "city plot"));
    CityModel m;
    m.dim = rho.rows;
    m.nQubits = n;
    m.quantity = quantity;
    m.maxAbs = largestElement(rho);
    if (m.maxAbs <= 0.0)
        return m;
    const double cutoff = std::max(0.0, relativeCutoff) * m.maxAbs;
    for (std::size_t i = 0; i < m.dim; ++i)
        for (std::size_t j = 0; j < m.dim; ++j) {
            const num::Complex v = rho(i, j);
            CityBar bar;
            bar.row = static_cast<std::uint32_t>(i);
            bar.col = static_cast<std::uint32_t>(j);
            bar.value = v;
            bar.diagonal = i == j;
            switch (quantity) {
            case CityQuantity::Real:
                bar.height = v.real();
                bar.color = divergingColor(bar.height / m.maxAbs);
                break;
            case CityQuantity::Imag:
                bar.height = v.imag();
                bar.color = divergingColor(bar.height / m.maxAbs);
                break;
            case CityQuantity::Magnitude:
                bar.height = std::abs(v);
                bar.color = sequentialColor(bar.height / m.maxAbs);
                break;
            case CityQuantity::Phase:
                bar.height = std::abs(v);
                bar.color = phaseColor(v);
                break;
            }
            if (std::abs(bar.height) > cutoff)
                m.bars.push_back(bar);
        }
    return m;
}

Result<HintonModel> buildHinton(const num::Matrix& rho, double relativeCutoff) {
    QXL_TRY_ASSIGN(const std::uint32_t n, checkedQubits(rho, "Hinton diagram"));
    HintonModel m;
    m.dim = rho.rows;
    m.nQubits = n;
    m.maxAbs = largestElement(rho);
    if (m.maxAbs <= 0.0)
        return m;
    const double cutoff = std::max(0.0, relativeCutoff) * m.maxAbs;
    for (std::size_t i = 0; i < m.dim; ++i)
        for (std::size_t j = 0; j < m.dim; ++j) {
            const num::Complex v = rho(i, j);
            const double a = std::abs(v);
            if (a <= cutoff)
                continue;
            HintonSquare s;
            s.row = static_cast<std::uint32_t>(i);
            s.col = static_cast<std::uint32_t>(j);
            s.value = v;
            s.side = std::sqrt(a / m.maxAbs); // area ∝ |ρ_ij|
            s.color = phaseColor(v);
            m.squares.push_back(s);
        }
    return m;
}

} // namespace qlab::viz::math
