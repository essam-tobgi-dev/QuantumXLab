// Spec 17 §6.3 — site positions, pad sizes, readout resonators and couplers.
#include "Core/Random.hpp"
#include "Lab/ChipInternal.hpp"
#include "Lab/Generators.hpp"
#include "Lab/Types.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <glm/gtc/constants.hpp>
#include <map>

namespace qlab::lab::chipdetail {

namespace {

bool tunableTechnology(hw::Technology t) {
    return t == hw::Technology::TransmonTunable || t == hw::Technology::TransmonTunableCoupler;
}

std::map<std::uint32_t, std::vector<std::uint32_t>> adjacency(const hw::Device& dev) {
    std::map<std::uint32_t, std::vector<std::uint32_t>> adj;
    for (const auto& e : dev.edges) {
        adj[e.a].push_back(e.b);
        adj[e.b].push_back(e.a);
    }
    return adj;
}

// Fruchterman–Reingold with ideal edge length = pitch, linear cooling, then snapped to the quantum.
void springEmbed(ChipLayout& L, const hw::Device& dev, const ChipLayoutOptions& o) {
    const std::size_t n = L.qubits.size();
    core::Random rng(o.seed);
    double k = o.pitch_um, side = std::sqrt(static_cast<double>(n)) * k;
    std::vector<glm::dvec2> p(n);
    for (auto& q : p) q = {rng.uniform(0.0, side), rng.uniform(0.0, side)};
    double t0 = side / 4.0;
    for (int it = 0; it < o.springIterations; ++it) {
        std::vector<glm::dvec2> disp(n, glm::dvec2(0.0));
        for (std::size_t u = 0; u < n; ++u)
            for (std::size_t v = u + 1; v < n; ++v) {
                glm::dvec2 d = p[u] - p[v];
                double len = std::max(glm::length(d), 1e-3 * k);
                glm::dvec2 f = d / len * (k * k / len);
                disp[u] += f;
                disp[v] -= f;
            }
        for (const auto& e : dev.edges) {
            glm::dvec2 d = p[e.a] - p[e.b];
            double len = std::max(glm::length(d), 1e-3 * k);
            glm::dvec2 f = d / len * (len * len / k);
            disp[e.a] -= f;
            disp[e.b] += f;
        }
        double temp = t0 * (1.0 - static_cast<double>(it) / o.springIterations);
        for (std::size_t v = 0; v < n; ++v) {
            double len = glm::length(disp[v]);
            if (len > 0.0) p[v] += disp[v] / len * std::min(len, temp);
        }
    }
    for (std::size_t v = 0; v < n; ++v)
        L.qubits[v].pos_um = glm::round(p[v] / o.springQuantum_um) * o.springQuantum_um;
}

} // namespace

Status placeSites(ChipLayout& L, const hw::Device& dev, const hw::Calibration* cal, const ChipLayoutOptions& o) {
    const double pitch = o.pitch_um;
    bool tunable = tunableTechnology(dev.technology);
    L.qubits.resize(dev.qubits.size());
    double armSum = 0.0;
    int armCount = 0;
    for (const auto& q : dev.qubits) {
        QubitSite& s = L.qubits[q.index];
        s.index = q.index;
        s.coupler = q.kind == hw::QubitKind::Coupler;
        s.tunable = tunable;
        s.armWidth_um = o.armWidth_um;
        s.gap_um = o.padGap_um;
        double alpha = -300e6;
        if (cal)
            if (const auto* qc = cal->qubit(q.index); qc && qc->anharmonicity.value.v != 0.0) alpha = qc->anharmonicity.value.v;
        s.armLength_um = xmonArmLength_um(alpha, o.armWidth_um, o.padGap_um, o.epsEff);
        if (!s.coupler) armSum += s.armLength_um, ++armCount;
        s.pos_um = glm::dvec2(q.pos[0], q.pos[1]) * pitch;
    }
    for (auto& s : L.qubits)
        if (s.coupler) { // coupler_tunable: a transmon-like element at `scale` of the mean qubit pad
            s.armLength_um = o.couplerScale * (armCount ? armSum / armCount : s.armLength_um);
            s.armWidth_um *= o.couplerScale;
            s.gap_um *= o.couplerScale;
        }

    auto adj = adjacency(dev);
    switch (L.topology) {
    case ChipTopology::HeavyHex: { // site (r, c) → x = a (c + ½ (r mod 2)), y = a (√3/2) r with a = 2 pitch
        for (const auto& q : dev.qubits) {
            long x = std::lround(q.pos[0]), y = std::lround(q.pos[1]);
            if (y % 2 != 0) continue;
            long r = y / 2;
            L.qubits[q.index].pos_um = {pitch * (static_cast<double>(x) + static_cast<double>(r % 2)), pitch * std::sqrt(3.0) * r};
        }
        for (const auto& q : dev.qubits) { // row-edge (bridge) qubits at the midpoint of their neighbours
            if (std::lround(q.pos[1]) % 2 == 0) continue;
            const auto& nb = adj[q.index];
            L.qubits[q.index].pos_um = 0.5 * (L.qubits[nb[0]].pos_um + L.qubits[nb[1]].pos_um);
        }
        break;
    }
    case ChipTopology::Grid: break; // x = c·pitch, y = r·pitch; couplers keep their midpoint positions
    case ChipTopology::Linear: {
        std::uint32_t start = dev.qubits.front().index;
        for (const auto& q : dev.qubits)
            if (adj[q.index].size() <= 1) {
                start = q.index;
                break;
            }
        std::uint32_t prev = start, cur = start;
        for (std::size_t i = 0; i < dev.qubits.size(); ++i) {
            L.qubits[cur].pos_um = {pitch * static_cast<double>(i), 0.0};
            std::uint32_t next = cur;
            for (auto w : adj[cur])
                if (w != prev) next = w;
            if (next == cur) break;
            prev = cur;
            cur = next;
        }
        break;
    }
    case ChipTopology::SpringEmbedded: springEmbed(L, dev, o); break;
    }
    if (L.topology == ChipTopology::SpringEmbedded)
        for (std::size_t i = 0; i < L.qubits.size(); ++i)
            for (std::size_t j = i + 1; j < L.qubits.size(); ++j)
                if (glm::length(L.qubits[i].pos_um - L.qubits[j].pos_um) < L.qubits[i].radius_um() + L.qubits[j].radius_um() + o.clearance_um)
                    return fail(kErrChip, std::format("spring-embedded layout of '{}': pads of q{} and q{} overlap", dev.id, i, j));
    return {};
}

void placeResonators(ChipLayout& L, const hw::Device& dev, const ChipLayoutOptions& o) {
    const double cw = 0.5 * o.cpwWidth_um + o.cpwGap_um;
    const double pi = glm::pi<double>();
    auto wrap = [&](double a) { return std::remainder(a, 2.0 * pi); };
    for (const auto& site : L.qubits) {
        if (site.coupler) continue;
        const std::uint32_t q = site.index;
        ResonatorSite r;
        r.qubit = q;
        r.frequency_Hz = q < dev.readout.resonatorFrequencies.size() ? dev.readout.resonatorFrequencies[q].v : 7.0e9;
        r.length_um = quarterWaveLength_m(r.frequency_Hz, o.epsEff) * 1e6;
        r.pitch_um = o.meanderPitch_um;
        r.lead_um = o.meanderLead_um;

        std::vector<double> angles;
        for (const auto& other : L.qubits)
            if (other.index != q && glm::length(other.pos_um - site.pos_um) < 1.75 * o.pitch_um)
                angles.push_back(std::atan2(other.pos_um.y - site.pos_um.y, other.pos_um.x - site.pos_um.x));
        std::sort(angles.begin(), angles.end());
        struct Cand { double angle, gap; };
        std::vector<Cand> gaps;
        if (angles.empty()) gaps.push_back({-0.5 * pi, 2.0 * pi});
        for (std::size_t i = 0; i < angles.size(); ++i) {
            double a0 = angles[i], a1 = i + 1 < angles.size() ? angles[i + 1] : angles[0] + 2.0 * pi;
            gaps.push_back({wrap(0.5 * (a0 + a1)), a1 - a0});
        }
        std::stable_sort(gaps.begin(), gaps.end(), [&](const Cand& a, const Cand& b) {
            if (std::abs(a.gap - b.gap) > 1e-6) return a.gap > b.gap;
            return std::abs(wrap(a.angle - 0.25 * pi)) < std::abs(wrap(b.angle - 0.25 * pi));
        });
        std::vector<double> candidates;
        for (const auto& g : gaps)
            for (double d : {0.0, 15.0, -15.0, 30.0, -30.0}) candidates.push_back(g.angle + d * pi / 180.0);
        for (int k = 0; k < 8; ++k) candidates.push_back(0.25 * pi * k);

        // A meander of fixed electrical length can be folded long-and-thin or short-and-wide; both
        // the direction and the aspect are searched so a dense lattice still finds a free pocket.
        struct Shape {
            std::vector<glm::dvec2> local;
            double lenX = 0.0, amp = 0.0, request = 0.0;
        };
        std::vector<Shape> shapes;
        for (double factor : {1.0, 0.7, 1.4, 2.0, 0.5}) {
            Shape s;
            s.request = o.meanderAmplitude_um * factor;
            s.local = meanderCenterline(r.length_um, o.meanderPitch_um, s.request, o.meanderLead_um);
            if (s.local.size() < 4) continue;
            s.lenX = s.local.back().x;
            for (const auto& p : s.local) s.amp = std::max(s.amp, std::abs(p.y));
            shapes.push_back(std::move(s));
        }
        if (shapes.empty()) {
            L.diagnostics.push_back(std::format("resonator of q{}: length {:.0f} µm is too short to meander", q, r.length_um));
            continue;
        }
        auto footprintAt = [&](const Shape& sh, double angle) {
            glm::dvec2 dir = unit(angle);
            ChipRect f;
            f.angle_rad = angle;
            f.halfSize = {0.5 * sh.lenX + cw, sh.amp + cw};
            f.center = site.pos_um + dir * (site.radius_um() + o.clearance_um + cw + 0.5 * sh.lenX);
            return f;
        };
        auto conflicts = [&](const ChipRect& f) {
            int n = 0;
            // The footprint starts one clearance outside its own pad, which the circle test can
            // read as touching; only the other sites matter here.
            for (const auto& other : L.qubits)
                n += other.index != q && f.overlapsCircle(other.pos_um, other.radius_um() + o.clearance_um) ? 1 : 0;
            for (const auto& placed : L.resonators) n += f.overlaps(placed.footprint, o.clearance_um) ? 1 : 0;
            return n;
        };
        const Shape* bestShape = &shapes.front();
        double best = candidates.front();
        int bestConflicts = 1 << 30;
        for (const auto& sh : shapes) {
            for (double a : candidates) {
                int c = conflicts(footprintAt(sh, a));
                if (c < bestConflicts) best = a, bestShape = &sh, bestConflicts = c;
                if (c == 0) break;
            }
            if (bestConflicts == 0) break;
        }
        if (bestConflicts > 0) L.diagnostics.push_back(std::format("resonator of q{}: no conflict-free side", q));
        r.angle_rad = best;
        r.amplitude_um = bestShape->request;
        r.footprint = footprintAt(*bestShape, best);
        glm::dvec2 dir = unit(best), perp{-dir.y, dir.x};
        r.origin_um = site.pos_um + dir * (site.radius_um() + o.clearance_um + cw);
        for (const auto& p : bestShape->local) r.centerline_um.push_back(r.origin_um + dir * p.x + perp * p.y);
        r.tap_um = r.centerline_um.back();
        L.resonators.push_back(std::move(r));
    }
    auto groups = feedlineGroups(dev, L, o.maxPerFeedline);
    for (std::size_t g = 0; g < groups.size(); ++g)
        for (auto qi : groups[g])
            for (auto& r : L.resonators)
                if (r.qubit == qi) r.feedline = static_cast<int>(g);
}

void placeCouplers(ChipLayout& L, const hw::Device& dev, const ChipLayoutOptions& o) {
    auto padEdgeToward = [&](std::uint32_t from, glm::dvec2 target) {
        const auto& s = L.qubits[from];
        glm::dvec2 d = target - s.pos_um;
        double len = glm::length(d);
        return len > 0.0 ? s.pos_um + d / len * (s.armLength_um + 0.5 * s.gap_um) : s.pos_um;
    };
    for (std::size_t i = 0; i < dev.edges.size(); ++i) {
        const auto& e = dev.edges[i];
        CouplerSite c;
        c.edge = i;
        c.a = e.a;
        c.b = e.b;
        c.couplerQubit = e.coupler;
        if (e.coupler && *e.coupler < L.qubits.size()) {
            glm::dvec2 cp = L.qubits[*e.coupler].pos_um;
            for (std::uint32_t end : {e.a, e.b})
                c.stubs.push_back(CpwRoute{{padEdgeToward(*e.coupler, L.qubits[end].pos_um), padEdgeToward(end, cp)}});
        } else {
            c.stubs.push_back(CpwRoute{{padEdgeToward(e.a, L.qubits[e.b].pos_um), padEdgeToward(e.b, L.qubits[e.a].pos_um)}});
        }
        L.couplers.push_back(std::move(c));
    }
    (void)o;
}

std::vector<std::vector<std::uint32_t>> feedlineGroups(const hw::Device& dev, const ChipLayout& L, int maxPer) {
    std::vector<std::vector<std::uint32_t>> groups;
    for (const auto& f : dev.readout.feedlines) groups.push_back(f.qubits);
    if (!groups.empty()) return groups;
    std::vector<std::uint32_t> data;
    for (const auto& s : L.qubits)
        if (!s.coupler) data.push_back(s.index);
    std::stable_sort(data.begin(), data.end(), [&](std::uint32_t a, std::uint32_t b) {
        const auto& pa = L.qubits[a].pos_um;
        const auto& pb = L.qubits[b].pos_um;
        return pa.y != pb.y ? pa.y < pb.y : pa.x < pb.x;
    });
    std::size_t per = static_cast<std::size_t>(std::max(1, maxPer));
    for (std::size_t i = 0; i < data.size(); i += per)
        groups.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(i),
                            data.begin() + static_cast<std::ptrdiff_t>(std::min(data.size(), i + per)));
    return groups;
}

} // namespace qlab::lab::chipdetail
