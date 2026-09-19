// Spec 21 §3.17 — Wigner function by the Laguerre expression (see Wigner.hpp).
#include "Viz/Math/Wigner.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace qlab::viz::math {
namespace {

using num::Complex;

constexpr double kTwoOverPi = 2.0 / std::numbers::pi;

Status checkMode(const num::Matrix& rho) {
    if (rho.rows == 0 || rho.rows != rho.cols)
        return fail(ErrorCode::InvalidArgument, "Wigner: the mode density matrix must be square");
    if (rho.rows > kWignerMaxFock + 1)
        return fail(ErrorCode::OutOfRange,
                    "Wigner: Fock truncation above N_max = " + std::to_string(kWignerMaxFock));
    return {};
}

// Off-diagonals k = m − n that carry weight; a Fock or thermal state has only k = 0.
std::vector<std::size_t> activeDiagonals(const num::Matrix& rho) {
    std::vector<std::size_t> ks;
    for (std::size_t k = 0; k < rho.rows; ++k) {
        double w = 0.0;
        for (std::size_t n = 0; n + k < rho.rows; ++n)
            w = std::max(w, std::abs(rho(n + k, n)));
        if (k == 0 || w > 1e-15)
            ks.push_back(k);
    }
    return ks;
}

// W at α = r e^{iθ} with x = 4r². `scratch` holds l_n^{(k)}(x) for the current k.
double evaluate(const num::Matrix& rho, std::span<const std::size_t> diagonals, double r,
                double theta, std::vector<double>& scratch) {
    const std::size_t dim = rho.rows;
    const double x = 4.0 * r * r;
    double sum = 0.0;
    for (std::size_t k : diagonals) {
        const std::size_t count = dim - k; // n = 0 … N − k
        const double kd = static_cast<double>(k);
        // l_0^{(k)} = x^{k/2} e^{−x/2} / √(k!), in log space; x = 0 leaves only k = 0.
        double l0;
        if (x <= 0.0)
            l0 = k == 0 ? 1.0 : 0.0;
        else
            l0 = std::exp(0.5 * kd * std::log(x) - 0.5 * std::lgamma(kd + 1.0) - 0.5 * x);
        scratch[0] = l0;
        for (std::size_t n = 0; n + 1 < count; ++n) {
            const double nd = static_cast<double>(n);
            const double prev = n == 0 ? 0.0 : scratch[n - 1];
            scratch[n + 1] =
                ((2.0 * nd + 1.0 + kd - x) * scratch[n] - std::sqrt(nd * (nd + kd)) * prev) /
                std::sqrt((nd + 1.0) * (nd + 1.0 + kd));
        }
        const Complex rot = std::polar(1.0, -kd * theta); // (2α*)^k / |2α|^k
        double part = 0.0;
        for (std::size_t n = 0; n < count; ++n) {
            const double term = (rho(n + k, n) * rot).real() * scratch[n];
            part += (n & 1u) ? -term : term;
        }
        sum += k == 0 ? part : 2.0 * part; // ρ_nm W_nm is the complex conjugate of ρ_mn W_mn
    }
    return kTwoOverPi * sum;
}

} // namespace

Result<double> wignerAt(const num::Matrix& rho, num::Complex alpha) {
    QXL_TRY(checkMode(rho));
    std::vector<double> scratch(rho.rows);
    const auto ks = activeDiagonals(rho);
    return evaluate(rho, ks, std::abs(alpha), std::arg(alpha), scratch);
}

double WignerGrid::x(std::size_t ix) const {
    return axes.nx < 2 ? axes.xMin
                       : axes.xMin + (axes.xMax - axes.xMin) * static_cast<double>(ix) /
                                         static_cast<double>(axes.nx - 1);
}
double WignerGrid::p(std::size_t iy) const {
    return axes.ny < 2 ? axes.pMin
                       : axes.pMin + (axes.pMax - axes.pMin) * static_cast<double>(iy) /
                                         static_cast<double>(axes.ny - 1);
}
double WignerGrid::symmetricLimit() const {
    return std::max({std::abs(minValue), std::abs(maxValue), 1e-12});
}

Result<WignerGrid> wignerGrid(const num::Matrix& rho, const WignerOptions& o) {
    QXL_TRY(checkMode(rho));
    if (o.nx < 2 || o.ny < 2 || o.nx > kWignerMaxGrid || o.ny > kWignerMaxGrid)
        return fail(ErrorCode::OutOfRange, "Wigner: the grid must be between 2x2 and 201x201");
    if (!(o.xMax > o.xMin) || !(o.pMax > o.pMin))
        return fail(ErrorCode::InvalidArgument, "Wigner: empty phase-space range");
    WignerGrid g;
    g.axes = o;
    g.values.resize(o.nx * o.ny);
    std::vector<double> scratch(rho.rows);
    const auto ks = activeDiagonals(rho);
    const double invSqrt2 = 1.0 / std::numbers::sqrt2;
    for (std::size_t iy = 0; iy < o.ny; ++iy)
        for (std::size_t ix = 0; ix < o.nx; ++ix) {
            const Complex alpha(g.x(ix) * invSqrt2, g.p(iy) * invSqrt2);
            g.values[iy * o.nx + ix] = evaluate(rho, ks, std::abs(alpha), std::arg(alpha), scratch);
        }
    const auto [lo, hi] = std::minmax_element(g.values.begin(), g.values.end());
    g.minValue = *lo;
    g.maxValue = *hi;
    // Trapezoid rule; d²α = dx dp / 2.
    const double cell = 0.5 * (o.xMax - o.xMin) / static_cast<double>(o.nx - 1) *
                        (o.pMax - o.pMin) / static_cast<double>(o.ny - 1);
    double total = 0.0, absolute = 0.0;
    for (std::size_t iy = 0; iy < o.ny; ++iy)
        for (std::size_t ix = 0; ix < o.nx; ++ix) {
            const double wx = (ix == 0 || ix + 1 == o.nx) ? 0.5 : 1.0;
            const double wy = (iy == 0 || iy + 1 == o.ny) ? 0.5 : 1.0;
            const double v = g.values[iy * o.nx + ix];
            total += wx * wy * v;
            absolute += wx * wy * std::abs(v);
        }
    g.integral = total * cell;
    g.negativity = absolute * cell - 1.0;
    return g;
}

std::vector<double> photonNumberDistribution(const num::Matrix& rho) {
    std::vector<double> p(std::min(rho.rows, rho.cols));
    for (std::size_t n = 0; n < p.size(); ++n)
        p[n] = rho(n, n).real();
    return p;
}

} // namespace qlab::viz::math
