#include "Hardware/Device.hpp"
#include <algorithm>
#include <deque>
#include <format>
#include <set>

namespace qlab::hw {

std::string_view technologyName(Technology t) {
    switch (t) {
    case Technology::TransmonFixed:
        return "transmon_fixed";
    case Technology::TransmonTunable:
        return "transmon_tunable";
    case Technology::TransmonTunableCoupler:
        return "transmon_tunable_coupler";
    case Technology::IonChain:
        return "ion_chain";
    }
    return "?";
}
Result<Technology> technologyFromName(std::string_view s) {
    if (s == "transmon_fixed")
        return Technology::TransmonFixed;
    if (s == "transmon_tunable")
        return Technology::TransmonTunable;
    if (s == "transmon_tunable_coupler")
        return Technology::TransmonTunableCoupler;
    if (s == "ion_chain")
        return Technology::IonChain;
    return fail(ErrorCode::Hardware_ + 3, std::format("unknown technology '{}'", s));
}
bool isTransmon(Technology t) {
    return t != Technology::IonChain;
}

bool NativeGateSet::hasSingle(std::string_view g) const {
    return std::find(single.begin(), single.end(), g) != single.end();
}
bool NativeGateSet::hasTwo(std::string_view g) const {
    return std::find(two.begin(), two.end(), g) != two.end();
}

const NativeGateSet& allowedGates(Technology t) {
    static const NativeGateSet fixed{
        {"x", "sx", "rz", "id"}, {"cx", "ecr"}, "dispersive", "measure_conditional_x"};
    static const NativeGateSet tunable{
        {"x", "sx", "rz", "id"}, {"cz"}, "dispersive", "measure_conditional_x"};
    static const NativeGateSet coupler{
        {"x", "sx", "rz", "id"}, {"cz", "siswap"}, "dispersive", "measure_conditional_x"};
    static const NativeGateSet ions{{"rx", "ry", "rz"}, {"ms"}, "fluorescence", "optical_pumping"};
    switch (t) {
    case Technology::TransmonFixed:
        return fixed;
    case Technology::TransmonTunable:
        return tunable;
    case Technology::TransmonTunableCoupler:
        return coupler;
    case Technology::IonChain:
        return ions;
    }
    return fixed;
}

std::size_t Device::dataQubitCount() const {
    return static_cast<std::size_t>(
        std::count_if(qubits.begin(), qubits.end(),
                      [](const QubitInfo& q) { return q.kind == QubitKind::Data; }));
}
bool Device::isCoupler(std::uint32_t q) const {
    return hasQubit(q) && qubits[q].kind == QubitKind::Coupler;
}

bool Device::adjacent(std::uint32_t a, std::uint32_t b) const {
    if (a == b || !hasQubit(a) || !hasQubit(b))
        return false;
    if (allToAll)
        return !isCoupler(a) && !isCoupler(b);
    return edgeIndex(a, b).has_value();
}
std::optional<std::size_t> Device::edgeIndex(std::uint32_t a, std::uint32_t b) const {
    if (a == b || !hasQubit(a) || !hasQubit(b))
        return std::nullopt;
    if (allToAll)
        return isCoupler(a) || isCoupler(b) ? std::nullopt : std::optional<std::size_t>(0);
    for (std::size_t i = 0; i < edges.size(); ++i)
        if ((edges[i].a == a && edges[i].b == b) || (edges[i].a == b && edges[i].b == a))
            return i;
    return std::nullopt;
}
bool Device::nativeDirection(std::uint32_t ctrl, std::uint32_t target) const {
    auto e = edgeIndex(ctrl, target);
    if (!e)
        return false;
    if (allToAll)
        return true;
    const EdgeInfo& ed = edges[*e];
    return !ed.directed || (ed.a == ctrl && ed.b == target);
}
std::vector<std::uint32_t> Device::neighbours(std::uint32_t q) const {
    std::vector<std::uint32_t> out;
    if (!hasQubit(q) || isCoupler(q))
        return out;
    if (allToAll) {
        for (const auto& o : qubits)
            if (o.index != q && o.kind != QubitKind::Coupler)
                out.push_back(o.index);
        return out;
    }
    for (const auto& e : edges) {
        if (e.a == q)
            out.push_back(e.b);
        else if (e.b == q)
            out.push_back(e.a);
    }
    std::sort(out.begin(), out.end());
    return out;
}
int Device::distance(std::uint32_t a, std::uint32_t b) const {
    if (!hasQubit(a) || !hasQubit(b))
        return -1;
    if (a == b)
        return 0;
    if (allToAll)
        return adjacent(a, b) ? 1 : -1;
    std::vector<int> dist(qubits.size(), -1);
    std::deque<std::uint32_t> queue{a};
    dist[a] = 0;
    while (!queue.empty()) {
        auto u = queue.front();
        queue.pop_front();
        for (auto v : neighbours(u))
            if (dist[v] < 0) {
                dist[v] = dist[u] + 1;
                if (v == b)
                    return dist[v];
                queue.push_back(v);
            }
    }
    return -1;
}
std::vector<std::vector<int>> Device::distanceMatrix() const {
    std::size_t n = qubits.size();
    std::vector<std::vector<int>> m(n, std::vector<int>(n, -1));
    for (std::uint32_t i = 0; i < n; ++i) {
        if (isCoupler(i))
            continue;
        std::vector<int> dist(n, -1);
        std::deque<std::uint32_t> queue{i};
        dist[i] = 0;
        while (!queue.empty()) {
            auto u = queue.front();
            queue.pop_front();
            for (auto v : neighbours(u))
                if (dist[v] < 0) {
                    dist[v] = dist[u] + 1;
                    queue.push_back(v);
                }
        }
        m[i] = dist;
    }
    return m;
}
std::vector<std::uint32_t> Device::dataQubits() const {
    std::vector<std::uint32_t> v;
    for (const auto& q : qubits)
        if (q.kind == QubitKind::Data)
            v.push_back(q.index);
    return v;
}
std::optional<int> Device::feedlineOf(std::uint32_t q) const {
    for (const auto& f : readout.feedlines)
        if (std::find(f.qubits.begin(), f.qubits.end(), q) != f.qubits.end())
            return f.id;
    return std::nullopt;
}
units::Time Device::activeResetDuration(units::Time xDuration) const {
    if (control.activeResetDuration)
        return *control.activeResetDuration;
    return timing.readout + control.feedbackLatency + xDuration;
}

Result<void> Device::validate() const {
    std::vector<std::string> bad;
    for (std::size_t i = 0; i < qubits.size(); ++i)
        if (qubits[i].index != i)
            bad.push_back(
                std::format("qubits[{}].index = {} (must equal position)", i, qubits[i].index));
    std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const auto& e = edges[i];
        if (e.kind == EdgeKind::AllToAll)
            continue;
        if (!hasQubit(e.a) || !hasQubit(e.b)) {
            bad.push_back(std::format("edges[{}] references unknown qubit ({}, {})", i, e.a, e.b));
            continue;
        }
        if (e.a == e.b)
            bad.push_back(std::format("edges[{}] is a self-loop on {}", i, e.a));
        if (isCoupler(e.a) || isCoupler(e.b))
            bad.push_back(std::format("edges[{}] joins a coupler qubit", i));
        auto key = std::minmax(e.a, e.b);
        if (!seen.insert(key).second)
            bad.push_back(
                std::format("edges[{}] duplicates pair ({}, {})", i, key.first, key.second));
        if (e.kind == EdgeKind::TunableCoupler) {
            if (!e.coupler)
                bad.push_back(std::format("edges[{}] tunable_coupler without 'coupler'", i));
            else if (!hasQubit(*e.coupler) || !isCoupler(*e.coupler))
                bad.push_back(
                    std::format("edges[{}] coupler {} is not a coupler qubit", i, *e.coupler));
        }
        if (technology == Technology::TransmonTunableCoupler && e.kind != EdgeKind::TunableCoupler)
            bad.push_back(std::format("edges[{}] must be tunable_coupler on this technology", i));
    }
    if (technology == Technology::IonChain && !allToAll)
        bad.push_back("ion_chain device needs the all_to_all pseudo-edge");
    const auto& allowed = allowedGates(technology);
    for (const auto& g : gates.single)
        if (!allowed.hasSingle(g))
            bad.push_back(std::format("native single-qubit gate '{}' not allowed for {}", g,
                                      technologyName(technology)));
    for (const auto& g : gates.two)
        if (!allowed.hasTwo(g))
            bad.push_back(std::format("native two-qubit gate '{}' not allowed for {}", g,
                                      technologyName(technology)));
    if (gates.two.empty())
        bad.push_back("no native two-qubit gate");
    if (quditDimension < 2)
        bad.push_back("qudit_dimension < 2");
    if (timing.dtPs <= 0)
        bad.push_back("timing.dt_ps must be positive");
    for (const auto& f : readout.feedlines)
        for (auto q : f.qubits)
            if (!hasQubit(q))
                bad.push_back(std::format("feedline {} references unknown qubit {}", f.id, q));
    for (const auto& c : crosstalk)
        if (!hasQubit(c.from) || !hasQubit(c.to))
            bad.push_back("crosstalk entry references unknown qubit");
    if (bad.empty())
        return {};
    Error e(ErrorCode::Hardware_ + 4,
            std::format("device '{}' failed validation with {} violation(s)", id, bad.size()));
    for (auto& b : bad)
        e.notes.push_back(std::move(b));
    return std::unexpected(std::move(e));
}

} // namespace qlab::hw
