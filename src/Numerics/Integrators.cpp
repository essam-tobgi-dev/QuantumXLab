#include "Numerics/Integrators.hpp"
#include "Numerics/Expm.hpp"
#include <algorithm>
#include <cmath>
namespace qlab::num {

void rk4Step(const OdeRhs& f, Vector& y, double t, double h) {
    std::size_t n = y.size();
    Vector k1(n), k2(n), k3(n), k4(n), tmp(n);
    f(t, y, k1);
    for (std::size_t i = 0; i < n; ++i) tmp[i] = y[i] + 0.5 * h * k1[i];
    f(t + 0.5 * h, tmp, k2);
    for (std::size_t i = 0; i < n; ++i) tmp[i] = y[i] + 0.5 * h * k2[i];
    f(t + 0.5 * h, tmp, k3);
    for (std::size_t i = 0; i < n; ++i) tmp[i] = y[i] + h * k3[i];
    f(t + h, tmp, k4);
    for (std::size_t i = 0; i < n; ++i) y[i] += (h / 6.0) * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
}

AdaptiveResult dopri5(const OdeRhs& f, Vector& y, double t0, double t1, double rtol, double atol, double hInit,
                      double hMax, const std::function<void(double, std::span<const Complex>)>& observer) {
    // Dormand–Prince tableau.
    constexpr double c2 = 1.0 / 5, c3 = 3.0 / 10, c4 = 4.0 / 5, c5 = 8.0 / 9;
    constexpr double a21 = 1.0 / 5;
    constexpr double a31 = 3.0 / 40, a32 = 9.0 / 40;
    constexpr double a41 = 44.0 / 45, a42 = -56.0 / 15, a43 = 32.0 / 9;
    constexpr double a51 = 19372.0 / 6561, a52 = -25360.0 / 2187, a53 = 64448.0 / 6561, a54 = -212.0 / 729;
    constexpr double a61 = 9017.0 / 3168, a62 = -355.0 / 33, a63 = 46732.0 / 5247, a64 = 49.0 / 176, a65 = -5103.0 / 18656;
    constexpr double b1 = 35.0 / 384, b3 = 500.0 / 1113, b4 = 125.0 / 192, b5 = -2187.0 / 6784, b6 = 11.0 / 84;
    constexpr double e1 = 71.0 / 57600, e3 = -71.0 / 16695, e4 = 71.0 / 1920, e5 = -17253.0 / 339200, e6 = 22.0 / 525, e7 = -1.0 / 40;
    AdaptiveResult res;
    std::size_t n = y.size();
    double span = t1 - t0;
    if (span <= 0 || n == 0) return res;
    if (hMax <= 0) hMax = span;
    double h = hInit > 0 ? std::min(hInit, hMax) : std::min(hMax, span / 100.0);
    double t = t0;
    Vector k1(n), k2(n), k3(n), k4(n), k5(n), k6(n), k7(n), tmp(n), ynew(n);
    f(t, y, k1);
    if (observer) observer(t, y);
    while (t < t1) {
        if (t + h > t1) h = t1 - t;
        for (std::size_t i = 0; i < n; ++i) tmp[i] = y[i] + h * a21 * k1[i];
        f(t + c2 * h, tmp, k2);
        for (std::size_t i = 0; i < n; ++i) tmp[i] = y[i] + h * (a31 * k1[i] + a32 * k2[i]);
        f(t + c3 * h, tmp, k3);
        for (std::size_t i = 0; i < n; ++i) tmp[i] = y[i] + h * (a41 * k1[i] + a42 * k2[i] + a43 * k3[i]);
        f(t + c4 * h, tmp, k4);
        for (std::size_t i = 0; i < n; ++i) tmp[i] = y[i] + h * (a51 * k1[i] + a52 * k2[i] + a53 * k3[i] + a54 * k4[i]);
        f(t + c5 * h, tmp, k5);
        for (std::size_t i = 0; i < n; ++i) tmp[i] = y[i] + h * (a61 * k1[i] + a62 * k2[i] + a63 * k3[i] + a64 * k4[i] + a65 * k5[i]);
        f(t + h, tmp, k6);
        for (std::size_t i = 0; i < n; ++i) ynew[i] = y[i] + h * (b1 * k1[i] + b3 * k3[i] + b4 * k4[i] + b5 * k5[i] + b6 * k6[i]);
        f(t + h, ynew, k7);
        double err = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            Complex ei = h * (e1 * k1[i] + e3 * k3[i] + e4 * k4[i] + e5 * k5[i] + e6 * k6[i] + e7 * k7[i]);
            double sc = atol + rtol * std::max(std::abs(y[i]), std::abs(ynew[i]));
            double r = std::abs(ei) / sc;
            err += r * r;
        }
        err = std::sqrt(err / static_cast<double>(n));
        if (err <= 1.0 || h < 1e-15 * std::max(1.0, std::abs(t))) {
            t += h;
            y.swap(ynew);
            k1.swap(k7); // FSAL
            ++res.steps;
            if (observer) observer(t, y);
            double fac = err == 0.0 ? 5.0 : std::min(5.0, std::max(0.2, 0.9 * std::pow(err, -0.2)));
            h = std::min(h * fac, hMax);
        } else {
            ++res.rejected;
            h *= std::max(0.2, 0.9 * std::pow(err, -0.2));
            if (res.rejected > 100000) { res.ok = false; break; }
        }
    }
    res.lastStep = h;
    return res;
}

void magnus2Step(std::size_t dim,
                 const std::function<void(double, std::span<const Complex>, std::span<Complex>)>& applyL,
                 Vector& y, double t, double h, std::size_t maxKrylov) {
    double tm = t + 0.5 * h;
    y = expmTimesVector(dim, [&](std::span<const Complex> x, std::span<Complex> out) {
        applyL(tm, x, out);
        for (auto& v : out) v *= h;
    }, y, 1e-13, maxKrylov);
}
} // namespace qlab::num
