// Spec 17 §6.3 — feedlines with Purcell filters, drive lines from the nearest chip edge, flux lines
// from the opposite edge; a grid A* router keeps CPWs off pads, resonators and bond pads while
// allowing crossings (bridged later) at a usage penalty.
#include "Lab/ChipInternal.hpp"
#include "Lab/Generators.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <queue>

namespace qlab::lab::chipdetail {

namespace {

class Router {
public:
    Router(const ChipLayout& L, const ChipLayoutOptions& o) : cell_(o.routeGrid_um) {
        nx_ = std::max(1, static_cast<int>(std::ceil(L.size_um.x / cell_)));
        ny_ = std::max(1, static_cast<int>(std::ceil(L.size_um.y / cell_)));
        blocked_.assign(static_cast<std::size_t>(nx_ * ny_), 0);
        usage_.assign(blocked_.size(), 0);
        double cw = 0.5 * o.cpwWidth_um + o.cpwGap_um + o.clearance_um;
        for (int j = 0; j < ny_; ++j)
            for (int i = 0; i < nx_; ++i) {
                glm::dvec2 c = centre(i, j);
                bool b = false;
                for (const auto& q : L.qubits) b = b || glm::length(c - q.pos_um) < q.radius_um() + cw;
                for (const auto& r : L.resonators) b = b || r.footprint.contains(c, cw);
                for (const auto& p : L.bondPads) {
                    glm::dvec2 d = glm::abs(c - p.pos_um);
                    b = b || (d.x < 0.5 * o.bondPadSize_um + cw && d.y < 0.5 * o.bondPadSize_um + cw);
                }
                blocked_[idx(i, j)] = b ? 1 : 0;
            }
    }
    void blockRect(const ChipRect& r, double inflate) {
        for (int j = 0; j < ny_; ++j)
            for (int i = 0; i < nx_; ++i)
                if (r.contains(centre(i, j), inflate)) blocked_[idx(i, j)] = 1;
    }
    // Shortest penalised path; endpoints are freed within `freeRadius`. Empty when unreachable.
    std::vector<glm::dvec2> route(glm::dvec2 from, glm::dvec2 to, double freeRadius) {
        int si = cellX(from.x), sj = cellY(from.y), gi = cellX(to.x), gj = cellY(to.y);
        const int S = nx_ * ny_;
        std::vector<double> g(static_cast<std::size_t>(S) * 5, std::numeric_limits<double>::infinity());
        std::vector<int> parent(static_cast<std::size_t>(S) * 5, -1);
        using QE = std::pair<double, int>;
        std::priority_queue<QE, std::vector<QE>, std::greater<>> open;
        auto h = [&](int i, int j) { return cell_ * (std::abs(i - gi) + std::abs(j - gj)); };
        int start = idx(si, sj) * 5 + 4;
        g[static_cast<std::size_t>(start)] = 0.0;
        open.push({h(si, sj), start});
        const int di[4] = {1, 0, -1, 0}, dj[4] = {0, 1, 0, -1};
        int goalState = -1;
        while (!open.empty()) {
            auto [f, st] = open.top();
            open.pop();
            int cellIdx = st / 5, dir = st % 5, i = cellIdx % nx_, j = cellIdx / nx_;
            if (f - h(i, j) > g[static_cast<std::size_t>(st)] + 1e-9) continue;
            if (i == gi && j == gj) {
                goalState = st;
                break;
            }
            for (int d = 0; d < 4; ++d) {
                int ni = i + di[d], nj = j + dj[d];
                if (ni < 0 || nj < 0 || ni >= nx_ || nj >= ny_) continue;
                glm::dvec2 c = centre(ni, nj);
                bool free = glm::length(c - from) <= freeRadius || glm::length(c - to) <= freeRadius;
                if (blocked_[idx(ni, nj)] && !free) continue;
                double step = cell_ * (1.0 + 3.0 * usage_[idx(ni, nj)]) + ((dir != 4 && dir != d) ? 0.6 * cell_ : 0.0);
                int ns = idx(ni, nj) * 5 + d;
                double ng = g[static_cast<std::size_t>(st)] + step;
                if (ng < g[static_cast<std::size_t>(ns)]) {
                    g[static_cast<std::size_t>(ns)] = ng;
                    parent[static_cast<std::size_t>(ns)] = st;
                    open.push({ng + h(ni, nj), ns});
                }
            }
        }
        std::vector<glm::dvec2> path;
        if (goalState < 0) return path;
        std::vector<int> cells;
        for (int st = goalState; st >= 0; st = parent[static_cast<std::size_t>(st)]) cells.push_back(st / 5);
        std::reverse(cells.begin(), cells.end());
        for (int c : cells) usage_[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::min(200, usage_[static_cast<std::size_t>(c)] + 1));
        path.push_back(from);
        for (std::size_t k = 1; k + 1 < cells.size(); ++k) { // keep corner cells only
            glm::dvec2 a = centre(cells[k - 1] % nx_, cells[k - 1] / nx_), b = centre(cells[k] % nx_, cells[k] / nx_),
                       c = centre(cells[k + 1] % nx_, cells[k + 1] / nx_);
            if (glm::length(glm::normalize(b - a) - glm::normalize(c - b)) > 1e-9) path.push_back(b);
        }
        path.push_back(to);
        return path;
    }

private:
    std::size_t idx(int i, int j) const { return static_cast<std::size_t>(j * nx_ + i); }
    glm::dvec2 centre(int i, int j) const { return {(i + 0.5) * cell_, (j + 0.5) * cell_}; }
    int cellX(double x) const { return std::clamp(static_cast<int>(x / cell_), 0, nx_ - 1); }
    int cellY(double y) const { return std::clamp(static_cast<int>(y / cell_), 0, ny_ - 1); }
    double cell_;
    int nx_ = 1, ny_ = 1;
    std::vector<std::uint8_t> blocked_, usage_;
};

glm::dvec2 edgeAxis(int edge) {
    static const glm::dvec2 axes[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    return axes[edge % 4];
}

int nearestEdge(glm::dvec2 p, glm::dvec2 size) {
    double d[4] = {p.y, size.x - p.x, size.y - p.y, p.x};
    return static_cast<int>(std::min_element(d, d + 4) - d);
}

int takePad(ChipLayout& L, int edge, glm::dvec2 near, int signal) {
    int best = -1;
    double bestD = 1e300;
    for (int pass = 0; pass < 2 && best < 0; ++pass)
        for (std::size_t k = 0; k < L.bondPads.size(); ++k) {
            const auto& b = L.bondPads[k];
            if (b.signal >= 0 || (pass == 0 && b.edge != edge)) continue;
            double dist = glm::length(b.pos_um - near);
            if (dist < bestD) bestD = dist, best = static_cast<int>(k);
        }
    if (best >= 0) L.bondPads[static_cast<std::size_t>(best)].signal = signal;
    return best;
}

} // namespace

void routeLines(ChipLayout& L, const hw::Device& dev, const ChipLayoutOptions& o) {
    Router router(L, o);
    const double padReach = 0.5 * o.bondPadSize_um;
    const double freePad = 0.5 * o.bondPadSize_um + 2.0 * o.routeGrid_um;
    auto connect = [&](std::vector<glm::dvec2>& path, glm::dvec2 a, glm::dvec2 b, double freeR, bool& ok) {
        std::vector<glm::dvec2> seg = router.route(a, b, freeR);
        if (seg.empty()) {
            ok = false;
            seg = {a, b};
        }
        if (!path.empty()) seg.erase(seg.begin());
        path.insert(path.end(), seg.begin(), seg.end());
    };
    auto groups = feedlineGroups(dev, L, o.maxPerFeedline);
    for (std::size_t g = 0; g < groups.size(); ++g) {
        FeedlineSite f;
        f.id = static_cast<int>(g);
        f.qubits = groups[g];
        std::vector<glm::dvec2> taps;
        for (const auto& r : L.resonators)
            if (r.feedline == f.id) taps.push_back(r.tap_um);
        if (taps.empty()) continue;
        std::sort(taps.begin(), taps.end(), [](glm::dvec2 a, glm::dvec2 b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
        auto sig = [&](const char* end) {
            L.signalNames.push_back(std::format("feedline[{}].{}", f.id, end));
            return static_cast<int>(L.signalNames.size() - 1);
        };
        f.padIn = takePad(L, 3, {0.0, taps.front().y}, sig("in"));
        f.padOut = takePad(L, 1, {L.size_um.x, taps.back().y}, sig("out"));
        glm::dvec2 in = f.padIn >= 0 ? L.bondPads[static_cast<std::size_t>(f.padIn)].pos_um : glm::dvec2(0.0, taps.front().y);
        glm::dvec2 out = f.padOut >= 0 ? L.bondPads[static_cast<std::size_t>(f.padOut)].pos_um : glm::dvec2(L.size_um.x, taps.back().y);
        // λ/4 Purcell filter in series at the feedline input, folded along the left edge above the pad
        double fMean = 0.0;
        for (auto q : f.qubits) fMean += q < dev.readout.resonatorFrequencies.size() ? dev.readout.resonatorFrequencies[q].v : 7e9;
        fMean /= static_cast<double>(std::max<std::size_t>(1, f.qubits.size()));
        std::vector<glm::dvec2> local = meanderCenterline(quarterWaveLength_m(fMean, o.epsEff) * 1e6, o.meanderPitch_um,
                                                          o.meanderAmplitude_um, o.meanderLead_um);
        double ampEff = 0.0;
        for (const auto& p : local) ampEff = std::max(ampEff, std::abs(p.y));
        glm::dvec2 origin = in + glm::dvec2(padReach + o.routeGrid_um, 0.0);
        for (const auto& p : local) f.purcell.path_um.push_back(origin + glm::dvec2(ampEff + p.y, p.x));
        ChipRect pf;
        pf.center = origin + glm::dvec2(ampEff, 0.5 * local.back().x);
        pf.halfSize = {0.5 * local.back().x, ampEff};
        pf.angle_rad = 0.5 * 3.141592653589793;
        router.blockRect(pf, o.cpwWidth_um + o.clearance_um);
        bool ok = true;
        std::vector<glm::dvec2> route{in + glm::dvec2(padReach, 0.0)};
        route.insert(route.end(), f.purcell.path_um.begin(), f.purcell.path_um.end());
        glm::dvec2 cursor = f.purcell.path_um.back();
        std::vector<glm::dvec2> rest{cursor};
        for (const auto& t : taps) {
            connect(rest, cursor, t, 0.5 * o.routeGrid_um + o.meanderAmplitude_um, ok);
            cursor = t;
        }
        connect(rest, cursor, out - glm::dvec2(padReach, 0.0), freePad, ok);
        route.insert(route.end(), rest.begin() + 1, rest.end());
        f.route.path_um = std::move(route);
        if (!ok) L.diagnostics.push_back(std::format("feedline {}: a segment could not be routed around obstacles", f.id));
        L.feedlines.push_back(std::move(f));
    }

    auto addLine = [&](std::uint32_t target, bool flux) {
        const QubitSite& s = L.qubits[target];
        int edge = nearestEdge(s.pos_um, L.size_um);
        if (flux) edge = (edge + 2) % 4;
        glm::dvec2 terminal = s.pos_um + edgeAxis(edge) * (s.radius_um() + o.clearance_um + 2.0 * (0.5 * o.cpwWidth_um + o.cpwGap_um));
        for (int turn = 1; turn < 4; ++turn) { // step around the pad if a resonator sits on that side
            bool inside = std::any_of(L.resonators.begin(), L.resonators.end(), [&](const ResonatorSite& r) { return r.footprint.contains(terminal, o.clearance_um); });
            if (!inside) break;
            terminal = s.pos_um + edgeAxis(edge + turn) * (s.radius_um() + o.clearance_um + 2.0 * (0.5 * o.cpwWidth_um + o.cpwGap_um));
        }
        ControlLine c;
        c.target = target;
        c.flux = flux;
        L.signalNames.push_back(std::format("{}[{}]", flux ? "flux" : "drive", target));
        c.bondPad = takePad(L, edge, terminal, static_cast<int>(L.signalNames.size() - 1));
        glm::dvec2 start = c.bondPad >= 0 ? L.bondPads[static_cast<std::size_t>(c.bondPad)].pos_um : terminal;
        glm::dvec2 inward = c.bondPad >= 0 ? -L.bondPads[static_cast<std::size_t>(c.bondPad)].outward : glm::dvec2(0.0);
        std::vector<glm::dvec2> seg = router.route(start + inward * padReach, terminal, freePad);
        c.routed = !seg.empty();
        c.route.path_um = c.routed ? seg : std::vector<glm::dvec2>{start + inward * padReach, terminal};
        if (!c.routed) L.diagnostics.push_back(std::format("{} line of q{}: drawn straight (unroutable)", flux ? "flux" : "drive", target));
        (flux ? L.fluxLines : L.driveLines).push_back(std::move(c));
    };
    for (const auto& s : L.qubits)
        if (!s.coupler) addLine(s.index, false);
    for (const auto& s : L.qubits)
        if (s.tunable || s.coupler) addLine(s.index, true);
}

} // namespace qlab::lab::chipdetail
