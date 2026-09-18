// Spec 17 §6.3 — chip outline, perimeter bond pads (all four edges, 200 µm pitch) and airbridges
// (every 200 µm along every routed CPW and at every crossing).
#include "Lab/ChipInternal.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <set>

namespace qlab::lab::chipdetail {

namespace {
int padsOnEdge(double length, const ChipLayoutOptions& o) {
    double corner = o.bondPadInset_um + o.bondPadPitch_um;
    return length > 2.0 * corner ? static_cast<int>(std::floor((length - 2.0 * corner) / o.bondPadPitch_um)) + 1 : 0;
}
void expandRect(glm::dvec2& lo, glm::dvec2& hi, const ChipRect& r) {
    glm::dvec2 u = unit(r.angle_rad), v{-u.y, u.x};
    for (double a : {-1.0, 1.0})
        for (double b : {-1.0, 1.0}) {
            glm::dvec2 p = r.center + u * (a * r.halfSize.x) + v * (b * r.halfSize.y);
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
}
} // namespace

void frameChip(ChipLayout& L, const ChipLayoutOptions& o, std::size_t signalsNeeded) {
    glm::dvec2 lo(1e300), hi(-1e300);
    for (const auto& q : L.qubits) {
        lo = glm::min(lo, q.pos_um - glm::dvec2(q.radius_um()));
        hi = glm::max(hi, q.pos_um + glm::dvec2(q.radius_um()));
    }
    for (const auto& r : L.resonators) expandRect(lo, hi, r.footprint);
    double margin = o.edgeMargin_um;
    glm::dvec2 size;
    for (;;) {
        size = hi - lo + glm::dvec2(2.0 * margin);
        auto pads = static_cast<std::size_t>(2 * padsOnEdge(size.x, o) + 2 * padsOnEdge(size.y, o));
        if (pads >= signalsNeeded + 8) break;
        margin += o.bondPadPitch_um;
    }
    glm::dvec2 shift = glm::dvec2(margin) - lo;
    for (auto& q : L.qubits) q.pos_um += shift;
    for (auto& r : L.resonators) {
        r.origin_um += shift;
        r.tap_um += shift;
        r.footprint.center += shift;
        for (auto& p : r.centerline_um) p += shift;
    }
    for (auto& c : L.couplers)
        for (auto& s : c.stubs)
            for (auto& p : s.path_um) p += shift;
    L.size_um = size;

    L.bondPads.clear();
    for (int edge = 0; edge < 4; ++edge) {
        double len = edge % 2 == 0 ? size.x : size.y;
        int n = padsOnEdge(len, o);
        double start = 0.5 * (len - (n - 1) * o.bondPadPitch_um);
        for (int k = 0; k < n; ++k) {
            double t = start + k * o.bondPadPitch_um;
            BondPadSite b;
            b.edge = edge;
            switch (edge) {
            case 0: b.pos_um = {t, o.bondPadInset_um}, b.outward = {0.0, -1.0}; break;
            case 1: b.pos_um = {size.x - o.bondPadInset_um, t}, b.outward = {1.0, 0.0}; break;
            case 2: b.pos_um = {t, size.y - o.bondPadInset_um}, b.outward = {0.0, 1.0}; break;
            default: b.pos_um = {o.bondPadInset_um, t}, b.outward = {-1.0, 0.0}; break;
            }
            L.bondPads.push_back(b);
        }
    }
}

namespace {
bool segmentIntersection(glm::dvec2 p, glm::dvec2 p2, glm::dvec2 q, glm::dvec2 q2, glm::dvec2& at) {
    glm::dvec2 r = p2 - p, s = q2 - q;
    double den = r.x * s.y - r.y * s.x;
    if (std::abs(den) < 1e-12) return false;
    glm::dvec2 qp = q - p;
    double t = (qp.x * s.y - qp.y * s.x) / den, u = (qp.x * r.y - qp.y * r.x) / den;
    if (t <= 1e-9 || t >= 1.0 - 1e-9 || u <= 1e-9 || u >= 1.0 - 1e-9) return false;
    at = p + t * r;
    return true;
}
} // namespace

void placeAirbridges(ChipLayout& L, const ChipLayoutOptions& o) {
    // Meandered resonators carry no bridges here: at a 50 µm meander pitch a 30 µm span would land
    // on the neighbouring run.
    std::vector<const CpwRoute*> routes;
    for (const auto& f : L.feedlines) routes.insert(routes.end(), {&f.route, &f.purcell});
    for (const auto& c : L.driveLines) routes.push_back(&c.route);
    for (const auto& c : L.fluxLines) routes.push_back(&c.route);
    for (const auto& c : L.couplers)
        for (const auto& s : c.stubs) routes.push_back(&s);
    L.airbridges.clear();
    const double pi = glm::pi<double>();
    for (const CpwRoute* r : routes) {
        const auto& P = r->path_um;
        double total = r->length_um();
        for (double s = 0.5 * o.airbridgePitch_um; s < total - 0.5 * o.airbridgePitch_um; s += o.airbridgePitch_um) {
            double acc = s;
            for (std::size_t i = 1; i < P.size(); ++i) {
                double len = glm::length(P[i] - P[i - 1]);
                if (acc > len) {
                    acc -= len;
                    continue;
                }
                glm::dvec2 dir = (P[i] - P[i - 1]) / len;
                L.airbridges.push_back({P[i - 1] + dir * acc, std::atan2(dir.y, dir.x) + 0.5 * pi, false});
                break;
            }
        }
    }
    // One bridge per crossing, merged when several crossings fall within a bridge span.
    std::set<std::pair<long, long>> taken;
    for (const auto& b : L.airbridges)
        taken.insert({std::lround(b.pos_um.x / o.airbridgePitch_um), std::lround(b.pos_um.y / o.airbridgePitch_um)});
    for (std::size_t a = 0; a < routes.size(); ++a)
        for (std::size_t b = a + 1; b < routes.size(); ++b) {
            const auto& A = routes[a]->path_um;
            const auto& B = routes[b]->path_um;
            for (std::size_t i = 1; i < A.size(); ++i)
                for (std::size_t j = 1; j < B.size(); ++j) {
                    glm::dvec2 at;
                    if (!segmentIntersection(A[i - 1], A[i], B[j - 1], B[j], at)) continue;
                    if (!taken.insert({std::lround(at.x / o.airbridgePitch_um), std::lround(at.y / o.airbridgePitch_um)}).second) continue;
                    glm::dvec2 dir = glm::normalize(B[j] - B[j - 1]); // the later route bridges over
                    L.airbridges.push_back({at, std::atan2(dir.y, dir.x), true});
                }
        }
}

} // namespace qlab::lab::chipdetail
