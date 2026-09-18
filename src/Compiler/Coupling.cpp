// Spec 14 §7–§8 — coupling graph, hop distances (BFS) and reliability distances (Floyd–Warshall).
#include "Compiler/Coupling.hpp"
#include "Compiler/Types.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <format>

namespace qlab::compiler {

Result<CouplingGraph> CouplingGraph::build(const hw::Device& device, const hw::Calibration* calibration) {
    CouplingGraph g;
    g.qubits_ = static_cast<std::uint32_t>(device.qubitCount());
    g.allToAll_ = device.allToAll;
    g.calibrated_ = calibration != nullptr;
    g.isData_.assign(g.qubits_, 0);
    for (const auto& q : device.qubits)
        if (q.kind == hw::QubitKind::Data && q.index < g.qubits_) {
            g.isData_[q.index] = 1;
            g.data_.push_back(q.index);
        }
    std::sort(g.data_.begin(), g.data_.end());
    if (g.data_.empty()) return fail(err::NoDevice, std::format("device '{}' has no data qubits", device.id));

    g.adj_.assign(g.qubits_, {});
    auto addEdge = [&](std::uint32_t a, std::uint32_t b) {
        if (a == b || !g.isData(a) || !g.isData(b)) return;
        if (std::find(g.adj_[a].begin(), g.adj_[a].end(), b) != g.adj_[a].end()) return;
        g.adj_[a].push_back(b);
        g.adj_[b].push_back(a);
        g.edges_.emplace_back(std::min(a, b), std::max(a, b));
    };
    if (device.allToAll) {
        for (std::size_t i = 0; i < g.data_.size(); ++i)
            for (std::size_t j = i + 1; j < g.data_.size(); ++j) addEdge(g.data_[i], g.data_[j]);
    } else {
        for (const auto& e : device.edges)
            if (e.kind != hw::EdgeKind::AllToAll) addEdge(e.a, e.b);
    }
    for (auto& list : g.adj_) std::sort(list.begin(), list.end());
    std::sort(g.edges_.begin(), g.edges_.end());

    // Hop distances: one BFS per data qubit.
    const std::size_t n = g.qubits_;
    g.hop_.assign(n * n, kUnreachable);
    std::deque<std::uint32_t> queue;
    for (std::uint32_t s : g.data_) {
        std::uint32_t* row = &g.hop_[s * n];
        row[s] = 0;
        queue.assign(1, s);
        while (!queue.empty()) {
            const std::uint32_t u = queue.front();
            queue.pop_front();
            for (std::uint32_t v : g.adj_[u])
                if (row[v] == kUnreachable) {
                    row[v] = row[u] + 1;
                    g.diameter_ = std::max(g.diameter_, row[v]);
                    queue.push_back(v);
                }
        }
    }

    // Edge costs −ln F_2q; an edge without a calibration entry takes the mean.
    std::vector<double> cost(g.edges_.size(), -1.0);
    double sum = 0.0, durations = 0.0;
    std::size_t known = 0;
    for (std::size_t k = 0; calibration && k < g.edges_.size(); ++k)
        if (const hw::EdgeCal* e = calibration->edge(g.edges_[k].first, g.edges_[k].second)) {
            const double r = std::clamp(e->gateError2q.value, 0.0, 0.999);
            cost[k] = -std::log1p(-r);
            sum += cost[k];
            durations += e->duration.value.si();
            ++known;
        }
    g.meanEdge_ = known > 0 && sum > 0.0 ? sum / static_cast<double>(known) : 1.0;
    g.typical2q_ = known > 0 ? durations / static_cast<double>(known) : 0.0;
    for (double& c : cost)
        if (c < 0.0) c = g.meanEdge_;
    g.edgeCost_ = cost;

    // Most reliable paths: Floyd–Warshall over the data qubits (≤ 127 on the shipped devices).
    const std::size_t d = g.data_.size();
    constexpr double inf = std::numeric_limits<double>::infinity();
    std::vector<double> dist(d * d, inf);
    std::vector<std::uint32_t> slot(n, 0);
    for (std::size_t i = 0; i < d; ++i) { slot[g.data_[i]] = static_cast<std::uint32_t>(i); dist[i * d + i] = 0.0; }
    for (std::size_t k = 0; k < g.edges_.size(); ++k) {
        const std::size_t a = slot[g.edges_[k].first], b = slot[g.edges_[k].second];
        dist[a * d + b] = dist[b * d + a] = cost[k];
    }
    for (std::size_t k = 0; k < d; ++k)
        for (std::size_t i = 0; i < d; ++i) {
            const double ik = dist[i * d + k];
            if (ik == inf) continue;
            for (std::size_t j = 0; j < d; ++j)
                if (ik + dist[k * d + j] < dist[i * d + j]) dist[i * d + j] = ik + dist[k * d + j];
        }
    g.rel_.assign(n * n, inf);
    for (std::size_t i = 0; i < d; ++i)
        for (std::size_t j = 0; j < d; ++j) g.rel_[g.data_[i] * n + g.data_[j]] = dist[i * d + j];

    g.readout_.assign(n, 0.0);
    g.decay_.assign(n, 0.0);
    for (std::uint32_t q : g.data_) {
        const hw::QubitCal* qc = calibration ? calibration->qubit(q) : nullptr;
        if (!qc) continue;
        g.readout_[q] = -std::log(std::clamp(qc->readoutFidelity(), 1e-6, 1.0));
        if (qc->t1.value.si() > 0.0) g.decay_[q] = 1.0 / qc->t1.value.si();
    }
    return g;
}

double CouplingGraph::edgeCost(std::uint32_t a, std::uint32_t b) const {
    const std::pair<std::uint32_t, std::uint32_t> key(std::min(a, b), std::max(a, b));
    const auto it = std::lower_bound(edges_.begin(), edges_.end(), key);
    if (it == edges_.end() || *it != key) return std::numeric_limits<double>::infinity();
    return edgeCost_[static_cast<std::size_t>(it - edges_.begin())];
}

} // namespace qlab::compiler
