#pragma once
// Spec 06 §8 — ODE integrators on complex vectors (T11 §5).
#include "Numerics/Matrix.hpp"
#include <functional>
#include <span>
namespace qlab::num {

// Right-hand side f(t, y, dydt).
using OdeRhs = std::function<void(double t, std::span<const Complex> y, std::span<Complex> dydt)>;

// One classical RK4 step: y ← y + h·Φ(t, y). Local error O(h⁵).
void rk4Step(const OdeRhs& f, Vector& y, double t, double h);

struct AdaptiveResult {
    std::size_t steps = 0;
    std::size_t rejected = 0;
    double lastStep = 0.0;
    bool ok = true;
};
// Dormand–Prince 5(4) from t0 to t1 with embedded error control:
// err = ‖y5 − y4‖ / (atol + rtol·‖y‖), h_new = h·min(5, max(0.2, 0.9·err^(−1/5))).
AdaptiveResult dopri5(const OdeRhs& f, Vector& y, double t0, double t1, double rtol = 1e-8,
                      double atol = 1e-10, double hInit = 0.0, double hMax = 0.0,
                      const std::function<void(double, std::span<const Complex>)>& observer = {});

// Exponential midpoint (Magnus order 2) for piecewise-constant generators: y(t+h) = exp(L h) y,
// where L is supplied as a matvec functor evaluated at the midpoint. Exact for constant L.
void magnus2Step(
    std::size_t dim,
    const std::function<void(double tmid, std::span<const Complex>, std::span<Complex>)>& applyL,
    Vector& y, double t, double h, std::size_t maxKrylov = 30);

} // namespace qlab::num
