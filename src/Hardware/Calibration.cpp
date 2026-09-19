#include "Hardware/Calibration.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <span>

namespace qlab::hw {

units::Time QubitCal::tphi() const {
    double inv = 1.0 / t2echo.value.v - 1.0 / (2.0 * t1.value.v);
    if (inv <= 0.0)
        return units::Time(INFINITY);
    return units::Time(1.0 / inv);
}
units::Temperature QubitCal::effectiveTemperature() const {
    return units::temperatureFromPopulation(f01.value, thermalPopulation.value);
}

std::string Calibration::edgeKey(std::uint32_t a, std::uint32_t b) {
    return std::format("{}-{}", a, b);
}
const EdgeCal* Calibration::edge(std::uint32_t a, std::uint32_t b) const {
    if (auto it = edges.find(edgeKey(a, b)); it != edges.end())
        return &it->second;
    if (auto it = edges.find(edgeKey(b, a)); it != edges.end())
        return &it->second;
    return nullptr;
}

Result<units::Time> Calibration::gateDuration(std::string_view gate,
                                              std::span<const std::uint32_t> qs) const {
    if (gate == "rz" || gate == "id" || gate == "barrier")
        return units::Time(0.0);
    if (qs.empty())
        return fail(ErrorCode::InvalidArgument, "gateDuration needs at least one qubit");
    if (qs.size() == 1) {
        auto* q = qubit(qs[0]);
        if (!q)
            return fail(ErrorCode::Hardware_ + 4,
                        std::format("no calibration for qubit {}", qs[0]));
        if (gate == "x" || gate == "rx" || gate == "ry" || gate == "y")
            return q->duration1q.value; // spec 10: x = 2 sx pulses merged into one 32 ns DRAG
        return q->duration1q.value;
    }
    auto* e = edge(qs[0], qs[1]);
    if (!e)
        return fail(ErrorCode::Hardware_ + 4,
                    std::format("no calibration for edge {}-{}", qs[0], qs[1]));
    if (auto it = e->extraGates.find(std::string(gate)); it != e->extraGates.end())
        return it->second.second.value;
    return e->duration.value;
}
Result<double> Calibration::gateError(std::string_view gate,
                                      std::span<const std::uint32_t> qs) const {
    if (gate == "rz" || gate == "id" || gate == "barrier")
        return 0.0;
    if (qs.empty())
        return fail(ErrorCode::InvalidArgument, "gateError needs at least one qubit");
    if (qs.size() == 1) {
        auto* q = qubit(qs[0]);
        if (!q)
            return fail(ErrorCode::Hardware_ + 4,
                        std::format("no calibration for qubit {}", qs[0]));
        return q->gateError1q.value;
    }
    auto* e = edge(qs[0], qs[1]);
    if (!e)
        return fail(ErrorCode::Hardware_ + 4,
                    std::format("no calibration for edge {}-{}", qs[0], qs[1]));
    if (auto it = e->extraGates.find(std::string(gate)); it != e->extraGates.end())
        return it->second.first.value;
    return e->gateError2q.value;
}
Result<Calibration::IdleParams> Calibration::idleParams(std::uint32_t qi) const {
    auto* q = qubit(qi);
    if (!q)
        return fail(ErrorCode::Hardware_ + 4, std::format("no calibration for qubit {}", qi));
    return IdleParams{q->t1.value, q->t2echo.value, q->t2star.value, q->tphi(),
                      q->thermalPopulation.value};
}
units::Time Calibration::maxT1() const {
    double m = 0.0;
    for (const auto& q : qubits)
        m = std::max(m, q.t1.value.v);
    return units::Time(m);
}

Result<void> Calibration::validate() const {
    std::vector<std::string> bad;
    for (std::size_t i = 0; i < qubits.size(); ++i) {
        const auto& q = qubits[i];
        const double t1 = q.t1.value.v, t2 = q.t2echo.value.v, t2s = q.t2star.value.v;
        if (!(t1 > 0.0))
            bad.push_back(std::format("qubit {}: t1 must be positive", i));
        if (t2 > 2.0 * t1 * (1.0 + 1e-9))
            bad.push_back(std::format("qubit {}: t2_echo {} > 2 t1 {}", i, t2, 2 * t1));
        if (t2s > t2 * (1.0 + 1e-9))
            bad.push_back(std::format("qubit {}: t2_star {} > t2_echo {}", i, t2s, t2));
        for (int r = 0; r < 2; ++r) {
            double s = q.readoutAssignment[r][0] + q.readoutAssignment[r][1];
            if (std::abs(s - 1.0) > 1e-6 || q.readoutAssignment[r][0] < 0 ||
                q.readoutAssignment[r][1] < 0)
                bad.push_back(
                    std::format("qubit {}: readout_assignment row {} is not stochastic", i, r));
        }
        if (q.thermalPopulation.value < 0.0 || q.thermalPopulation.value >= 0.5)
            bad.push_back(std::format("qubit {}: thermal_population out of [0, 0.5)", i));
        if (q.gateError1q.value < 0.0 || q.gateError1q.value > 1.0)
            bad.push_back(std::format("qubit {}: gate_error_1q out of [0,1]", i));
        if (q.f01.value.v <= 0.0)
            bad.push_back(std::format("qubit {}: f01 must be positive", i));
    }
    for (const auto& [k, e] : edges) {
        if (e.gateError2q.value < 0.0 || e.gateError2q.value > 1.0)
            bad.push_back(std::format("edge {}: gate_error_2q out of [0,1]", k));
        if (e.duration.value.v < 0.0)
            bad.push_back(std::format("edge {}: negative duration", k));
    }
    if (bad.empty())
        return {};
    Error err(
        ErrorCode::Hardware_ + 5,
        std::format("calibration '{}' failed validation with {} violation(s)", device, bad.size()));
    for (auto& b : bad)
        err.notes.push_back(std::move(b));
    return std::unexpected(std::move(err));
}

Calibration Calibration::perturbed(std::uint64_t seed, Spreads s) const {
    core::Random rng(seed);
    Calibration c = *this;
    auto lognormal = [&](double x, double sigmaLn) {
        return x * std::exp(rng.normal(0.0, sigmaLn));
    };
    for (auto& q : c.qubits) {
        q.f01.value.v += rng.normal(0.0, s.fSigmaHz);
        q.anharmonicity.value.v += rng.normal(0.0, s.alphaSigmaHz);
        q.t1.value.v = lognormal(q.t1.value.v, s.t1Ln);
        q.t2echo.value.v = std::min(lognormal(q.t2echo.value.v, s.t2Ln), 2.0 * q.t1.value.v * 0.98);
        q.t2star.value.v = q.t2echo.value.v * rng.uniform(0.4, 0.8);
        q.gateError1q.value = lognormal(q.gateError1q.value, s.errLn);
        q.thermalPopulation.value =
            std::min(0.49, lognormal(q.thermalPopulation.value, s.pThermalLn));
        for (int r = 0; r < 2; ++r) {
            double off = q.readoutAssignment[r][1 - r];
            off = std::clamp(off + rng.normal(0.0, s.readoutRel * off), 0.002, 0.1);
            q.readoutAssignment[r][1 - r] = off;
            q.readoutAssignment[r][r] = 1.0 - off;
        }
    }
    for (auto& [k, e] : c.edges)
        e.gateError2q.value = lognormal(e.gateError2q.value, s.errLn);
    return c;
}

} // namespace qlab::hw
