#include "Hardware/FrequencyPlan.hpp"
#include <cmath>
#include <format>
#include <set>

namespace qlab::hw {

bool isHardCollision(CollisionKind k) {
    return k != CollisionKind::NeighbourDetuning;
}

std::vector<Collision> FrequencyPlan::hardCollisions() const {
    std::vector<Collision> out;
    for (auto& c : check())
        if (c.hard)
            out.push_back(std::move(c));
    return out;
}

std::string_view collisionName(CollisionKind k) {
    switch (k) {
    case CollisionKind::Degenerate:
        return "degenerate";
    case CollisionKind::Straddle:
        return "straddle";
    case CollisionKind::TwoPhoton:
        return "two_photon";
    case CollisionKind::CrTooSlow:
        return "cr_too_slow";
    case CollisionKind::CrUnstable:
        return "cr_unstable";
    case CollisionKind::Spectator:
        return "spectator";
    case CollisionKind::NeighbourDetuning:
        return "neighbour_detuning";
    }
    return "?";
}

FrequencyPlan::FrequencyPlan(const Device& dev, const Calibration& cal, CollisionThresholds th)
    : dev_(dev), cal_(cal), th_(th) {}

std::vector<double> FrequencyPlan::frequencies() const {
    std::vector<double> f(dev_.qubits.size(), NAN);
    for (std::size_t i = 0; i < f.size() && i < cal_.qubits.size(); ++i)
        f[i] = cal_.qubits[i].f01.value.v;
    return f;
}

void FrequencyPlan::checkPair(std::uint32_t i, std::uint32_t j, bool coupled,
                              std::vector<Collision>& out) const {
    const auto* qi = cal_.qubit(i);
    const auto* qj = cal_.qubit(j);
    if (!qi || !qj)
        return;
    const double fi = qi->f01.value.v, fj = qj->f01.value.v, ai = qi->anharmonicity.value.v,
                 aj = qj->anharmonicity.value.v;
    auto add = [&](CollisionKind k, double val, double thr, std::optional<std::uint32_t> ctrl,
                   std::string msg) {
        out.push_back({k, i, j, ctrl, units::Frequency(val), units::Frequency(thr), std::move(msg),
                       isHardCollision(k)});
    };
    const double d = std::abs(fi - fj);
    if (d < th_.degenerate.v)
        add(CollisionKind::Degenerate, d, th_.degenerate.v, std::nullopt,
            std::format("q{} and q{} are {:.1f} MHz apart (< {:.0f} MHz)", i, j, d / 1e6,
                        th_.degenerate.v / 1e6));
    // Straddle: f_i + α_i/2 ≈ f_j (either ordering)
    const double s1 = std::abs(fi + ai / 2 - fj), s2 = std::abs(fj + aj / 2 - fi);
    if (s1 < th_.straddle.v)
        add(CollisionKind::Straddle, s1, th_.straddle.v, std::nullopt,
            std::format("q{} |0>-|2>/2 straddles q{}", i, j));
    else if (s2 < th_.straddle.v)
        add(CollisionKind::Straddle, s2, th_.straddle.v, std::nullopt,
            std::format("q{} |0>-|2>/2 straddles q{}", j, i));
    const double t1 = std::abs(2 * fi + ai - 2 * fj), t2 = std::abs(2 * fj + aj - 2 * fi);
    if (t1 < th_.twoPhoton.v)
        add(CollisionKind::TwoPhoton, t1, th_.twoPhoton.v, std::nullopt,
            std::format("two-photon resonance q{} 0-2 with 2×q{}", i, j));
    else if (t2 < th_.twoPhoton.v)
        add(CollisionKind::TwoPhoton, t2, th_.twoPhoton.v, std::nullopt,
            std::format("two-photon resonance q{} 0-2 with 2×q{}", j, i));
    if (coupled && dev_.technology == Technology::TransmonFixed) {
        if (d > th_.crMax.v)
            add(CollisionKind::CrTooSlow, d, th_.crMax.v, i,
                std::format("CR detuning q{}-q{} {:.0f} MHz exceeds {:.0f} MHz", i, j, d / 1e6,
                            th_.crMax.v / 1e6));
        if (d < th_.crMin.v)
            add(CollisionKind::CrUnstable, d, th_.crMin.v, i,
                std::format("CR detuning q{}-q{} {:.0f} MHz below {:.0f} MHz", i, j, d / 1e6,
                            th_.crMin.v / 1e6));
        if (d < dev_.frequencyPlan.minNeighbourDetuning.v && d >= th_.degenerate.v)
            add(CollisionKind::NeighbourDetuning, d, dev_.frequencyPlan.minNeighbourDetuning.v,
                std::nullopt, std::format("neighbours q{}-q{} closer than the plan minimum", i, j));
    }
}

std::vector<Collision> FrequencyPlan::check() const {
    std::vector<Collision> out;
    if (!isTransmon(dev_.technology))
        return out;
    std::set<std::pair<std::uint32_t, std::uint32_t>> done;
    auto pair = [&](std::uint32_t a, std::uint32_t b, bool coupled) {
        auto k = std::minmax(a, b);
        if (!done.insert(k).second)
            return;
        checkPair(k.first, k.second, coupled, out);
    };
    for (const auto& e : dev_.edges)
        if (e.kind != EdgeKind::AllToAll)
            pair(e.a, e.b, true);
    // Pairs sharing a neighbour (next-nearest); spectator rule for CR: control c drives at f_t,
    // spectator s of c must not be near f_t.
    for (const auto& q : dev_.qubits) {
        if (q.kind != QubitKind::Data)
            continue;
        auto nb = dev_.neighbours(q.index);
        for (std::size_t x = 0; x < nb.size(); ++x)
            for (std::size_t y = x + 1; y < nb.size(); ++y) {
                pair(nb[x], nb[y], false);
                if (dev_.technology == Technology::TransmonFixed) {
                    const auto* qt = cal_.qubit(nb[x]);
                    const auto* qs = cal_.qubit(nb[y]);
                    if (!qt || !qs)
                        continue;
                    double d = std::abs(qt->f01.value.v - qs->f01.value.v);
                    if (d < th_.spectator.v)
                        out.push_back(
                            {CollisionKind::Spectator, nb[x], nb[y], q.index, units::Frequency(d),
                             th_.spectator,
                             std::format(
                                 "spectator q{} of control q{} within {:.1f} MHz of target q{}",
                                 nb[y], q.index, d / 1e6, nb[x])});
                }
            }
    }
    return out;
}

} // namespace qlab::hw
