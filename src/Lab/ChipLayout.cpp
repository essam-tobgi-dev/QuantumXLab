// Spec 17 §6.3 — chip layout: topology classification, pad capacitance model, footprints, driver.
#include "Lab/ChipLayout.hpp"
#include "Lab/ChipInternal.hpp"
#include "Lab/Types.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <glm/gtc/constants.hpp>
#include <map>
#include <set>

namespace qlab::lab {

std::string_view chipTopologyName(ChipTopology t) {
    switch (t) {
    case ChipTopology::HeavyHex: return "heavy_hex";
    case ChipTopology::Grid: return "grid";
    case ChipTopology::Linear: return "linear";
    case ChipTopology::SpringEmbedded: return "spring";
    }
    return "?";
}

namespace {
bool integral(double v) { return std::abs(v - std::round(v)) < 1e-9; }
using Key = std::pair<long, long>;
Key keyOf(const hw::QubitInfo& q) { return {std::lround(q.pos[0]), std::lround(q.pos[1])}; }
} // namespace

ChipTopology classifyTopology(const hw::Device& dev) {
    std::vector<const hw::QubitInfo*> data;
    for (const auto& q : dev.qubits)
        if (q.kind != hw::QubitKind::Coupler) data.push_back(&q);
    const std::size_t n = data.size();
    if (n == 0 || dev.allToAll) return ChipTopology::SpringEmbedded;
    std::map<std::uint32_t, std::vector<std::uint32_t>> adj;
    for (const auto& e : dev.edges) {
        adj[e.a].push_back(e.b);
        adj[e.b].push_back(e.a);
    }
    std::size_t E = dev.edges.size(), maxDeg = 0;
    for (const auto* q : data) maxDeg = std::max(maxDeg, adj[q->index].size());
    bool allIntegral = std::all_of(data.begin(), data.end(), [](auto* q) { return integral(q->pos[0]) && integral(q->pos[1]); });
    std::map<Key, std::uint32_t> at;
    for (const auto* q : data) at[keyOf(*q)] = q->index;
    auto posOf = [&](std::uint32_t i) { return keyOf(dev.qubits[i]); };
    bool unitEdges = allIntegral && at.size() == n && std::all_of(dev.edges.begin(), dev.edges.end(), [&](const hw::EdgeInfo& e) {
        Key a = posOf(e.a), b = posOf(e.b);
        return std::abs(a.first - b.first) + std::abs(a.second - b.second) == 1;
    });
    if (unitEdges) { // full R × C rectangle with every Manhattan neighbour coupled
        long minX = 1L << 30, minY = 1L << 30, maxX = -(1L << 30), maxY = -(1L << 30);
        for (const auto& [k, i] : at) {
            minX = std::min(minX, k.first), maxX = std::max(maxX, k.first);
            minY = std::min(minY, k.second), maxY = std::max(maxY, k.second);
        }
        auto C = static_cast<std::size_t>(maxX - minX + 1), R = static_cast<std::size_t>(maxY - minY + 1);
        if (R * C == n && E == R * (C - 1) + C * (R - 1) && R > 1 && C > 1) return ChipTopology::Grid;
    }
    // path graph: n − 1 edges, degrees ≤ 2, connected
    if (E + 1 == n && maxDeg <= 2) {
        std::set<std::uint32_t> seen{data.front()->index};
        std::vector<std::uint32_t> stack{data.front()->index};
        while (!stack.empty()) {
            std::uint32_t v = stack.back();
            stack.pop_back();
            for (auto w : adj[v])
                if (seen.insert(w).second) stack.push_back(w);
        }
        if (seen.size() == n) return ChipTopology::Linear;
    }
    // heavy hex: rows on even y, degree-2 bridges on odd y joining (x, y±1), at least one cycle
    if (unitEdges && maxDeg <= 3 && E >= n) {
        bool ok = true;
        for (const auto* q : data) {
            Key k = keyOf(*q);
            if (k.second % 2 == 0) continue;
            const auto& nb = adj[q->index];
            auto up = at.find({k.first, k.second + 1}), down = at.find({k.first, k.second - 1});
            ok = ok && nb.size() == 2 && up != at.end() && down != at.end() &&
                 std::find(nb.begin(), nb.end(), up->second) != nb.end() &&
                 std::find(nb.begin(), nb.end(), down->second) != nb.end();
        }
        if (ok) return ChipTopology::HeavyHex;
    }
    return ChipTopology::SpringEmbedded;
}

double xmonArmLength_um(double anharmonicity_Hz, double armWidth_um, double gap_um, double epsEff) {
    constexpr double h = 6.62607015e-34, e = 1.602176634e-19, eps0 = 8.8541878128e-12;
    auto K = [](double k) { // complete elliptic integral of the first kind via the AGM
        double a = 1.0, b = std::sqrt(std::max(0.0, 1.0 - k * k));
        for (int i = 0; i < 40 && std::abs(a - b) > 1e-15; ++i) {
            double an = 0.5 * (a + b);
            b = std::sqrt(a * b);
            a = an;
        }
        return glm::pi<double>() / (2.0 * a);
    };
    double EC = h * std::max(std::abs(anharmonicity_Hz), 50e6); // transmon limit: E_C ≈ −α
    double Csum = e * e / (2.0 * EC);
    double k = armWidth_um / (armWidth_um + 2.0 * gap_um);
    double perLength = 4.0 * eps0 * epsEff * K(k) / K(std::sqrt(1.0 - k * k)); // F/m
    double L = Csum / (4.0 * perLength) * 1e6;
    return std::clamp(L, 60.0, 400.0);
}

double CpwRoute::length_um() const {
    double s = 0.0;
    for (std::size_t i = 1; i < path_um.size(); ++i) s += glm::length(path_um[i] - path_um[i - 1]);
    return s;
}

namespace {
glm::dvec2 axisOf(const ChipRect& r) { return {std::cos(r.angle_rad), std::sin(r.angle_rad)}; }
// Separating-axis test on the four candidate axes of two oriented rectangles.
bool separated(const ChipRect& A, const ChipRect& B, double clearance) {
    glm::dvec2 axes[4] = {axisOf(A), {-axisOf(A).y, axisOf(A).x}, axisOf(B), {-axisOf(B).y, axisOf(B).x}};
    glm::dvec2 d = B.center - A.center;
    for (const auto& ax : axes) {
        auto extent = [&](const ChipRect& r) {
            glm::dvec2 u = axisOf(r), v{-u.y, u.x};
            return r.halfSize.x * std::abs(glm::dot(u, ax)) + r.halfSize.y * std::abs(glm::dot(v, ax));
        };
        if (std::abs(glm::dot(d, ax)) > extent(A) + extent(B) + clearance) return true;
    }
    return false;
}
} // namespace

bool ChipRect::overlaps(const ChipRect& o, double clearance) const { return !separated(*this, o, clearance); }

bool ChipRect::contains(glm::dvec2 p, double inflate) const {
    glm::dvec2 u = axisOf(*this), v{-u.y, u.x}, d = p - center;
    return std::abs(glm::dot(d, u)) <= halfSize.x + inflate && std::abs(glm::dot(d, v)) <= halfSize.y + inflate;
}

bool ChipRect::overlapsCircle(glm::dvec2 c, double r) const {
    glm::dvec2 u = axisOf(*this), v{-u.y, u.x}, d = c - center;
    glm::dvec2 local{glm::dot(d, u), glm::dot(d, v)};
    glm::dvec2 q = glm::clamp(local, -halfSize, halfSize);
    return glm::length(local - q) < r;
}

std::vector<std::string> ChipLayout::overlaps(double clearance) const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < qubits.size(); ++i)
        for (std::size_t j = i + 1; j < qubits.size(); ++j)
            if (glm::length(qubits[i].pos_um - qubits[j].pos_um) < qubits[i].radius_um() + qubits[j].radius_um() + clearance)
                out.push_back(std::format("pads of q{} and q{} overlap", i, j));
    for (const auto& r : resonators) {
        for (const auto& q : qubits)
            if (r.footprint.overlapsCircle(q.pos_um, q.radius_um() + clearance))
                out.push_back(std::format("resonator of q{} overlaps the pad of q{}", r.qubit, q.index));
        for (const auto& s : resonators)
            if (s.qubit > r.qubit && r.footprint.overlaps(s.footprint, clearance))
                out.push_back(std::format("resonators of q{} and q{} overlap", r.qubit, s.qubit));
    }
    return out;
}

Result<ChipLayout> layoutChip(const hw::Device& dev, const hw::Calibration* cal, const ChipLayoutOptions& o) {
    if (!hw::isTransmon(dev.technology))
        return fail(kErrChip, std::format("device '{}' is not a superconducting chip", dev.id));
    if (dev.qubits.empty()) return fail(kErrChip, std::format("device '{}' has no qubits", dev.id));
    ChipLayout L;
    L.device = dev.id;
    L.topology = classifyTopology(dev);
    QXL_TRY(chipdetail::placeSites(L, dev, cal, o));
    chipdetail::placeResonators(L, dev, o);
    chipdetail::placeCouplers(L, dev, o);
    std::size_t signals = 0;
    for (const auto& q : L.qubits) signals += (q.coupler ? 0 : 1) + (q.tunable ? 1 : 0);
    signals += 2 * chipdetail::feedlineGroups(dev, L, o.maxPerFeedline).size();
    chipdetail::frameChip(L, o, signals);
    chipdetail::routeLines(L, dev, o);
    chipdetail::placeAirbridges(L, o);
    if (auto clash = L.overlaps(); !clash.empty()) {
        Error err(kErrChip, std::format("chip layout of '{}' ({}) has {} overlap(s)", dev.id,
                                        chipTopologyName(L.topology), clash.size()));
        for (auto& c : clash) err.withNote(std::move(c));
        return std::unexpected(std::move(err));
    }
    return L;
}

} // namespace qlab::lab
