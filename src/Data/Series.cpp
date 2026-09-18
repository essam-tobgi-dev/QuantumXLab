#include "Data/Series.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace qlab::data {

std::vector<std::pair<double, double>> decimate(std::span<const double> x, std::span<const double> y,
                                                double x0, double x1, std::size_t maxPoints) {
    std::vector<std::pair<double, double>> out;
    if (x.empty() || maxPoints == 0) return out;
    auto lo = std::lower_bound(x.begin(), x.end(), x0);
    auto hi = std::upper_bound(x.begin(), x.end(), x1);
    std::size_t b = static_cast<std::size_t>(lo - x.begin()), e = static_cast<std::size_t>(hi - x.begin());
    if (e <= b) return out;
    std::size_t n = e - b;
    if (n <= maxPoints) {
        out.reserve(n);
        for (std::size_t i = b; i < e; ++i) out.emplace_back(x[i], y[i]);
        return out;
    }
    std::size_t buckets = std::max<std::size_t>(1, maxPoints / 2);
    double span = x1 - x0;
    if (span <= 0) span = x[e - 1] - x[b];
    if (span <= 0) { out.emplace_back(x[b], y[b]); return out; }
    out.reserve(2 * buckets);
    std::size_t i = b;
    for (std::size_t k = 0; k < buckets && i < e; ++k) {
        double bx1 = (k + 1 == buckets) ? std::numeric_limits<double>::infinity() : x0 + span * static_cast<double>(k + 1) / static_cast<double>(buckets);
        std::size_t imin = i, imax = i;
        std::size_t j = i;
        for (; j < e && x[j] < bx1; ++j) {
            if (y[j] < y[imin]) imin = j;
            if (y[j] > y[imax]) imax = j;
        }
        if (j == i) continue;
        if (imin == imax) out.emplace_back(x[imin], y[imin]);
        else if (imin < imax) { out.emplace_back(x[imin], y[imin]); out.emplace_back(x[imax], y[imax]); }
        else { out.emplace_back(x[imax], y[imax]); out.emplace_back(x[imin], y[imin]); }
        i = j;
    }
    return out;
}

Series decimate(const Series& s, double x0, double x1, std::size_t maxPoints) {
    Series r; r.desc = s.desc; r.markers = s.markers;
    if (s.y.empty()) return r;
    // Envelope on the first component; other components sampled at the same indices is not
    // possible for min/max, so each component gets its own envelope on a shared x grid only
    // when arity == 1. For arity > 1 we decimate component 0 and carry the others by nearest x.
    auto env = decimate(s.x, s.y[0], x0, x1, maxPoints);
    r.x.reserve(env.size());
    r.y.assign(s.y.size(), {});
    for (auto& [px, py] : env) { r.x.push_back(px); r.y[0].push_back(py); }
    for (std::size_t k = 1; k < s.y.size(); ++k) {
        r.y[k].reserve(env.size());
        for (double px : r.x) {
            auto it = std::lower_bound(s.x.begin(), s.x.end(), px);
            std::size_t idx = static_cast<std::size_t>(it - s.x.begin());
            if (idx >= s.x.size()) idx = s.x.size() - 1;
            r.y[k].push_back(s.y[k][idx]);
        }
    }
    return r;
}
Series decimate(const Series& s, std::size_t maxPoints) {
    if (s.x.empty()) return s;
    return decimate(s, s.x.front(), s.x.back(), maxPoints);
}

ChannelId Recorder::add(ChannelDesc desc) {
    std::lock_guard lk(mu_);
    ChannelId id{static_cast<std::uint32_t>(entries_.size())};
    Entry e; e.desc = desc; e.y.assign(std::max<std::size_t>(1, desc.arity), {});
    entries_.push_back(std::move(e));
    byId_[desc.id] = id;
    return id;
}
std::optional<ChannelId> Recorder::find(std::string_view id) const {
    std::lock_guard lk(mu_);
    auto it = byId_.find(id);
    if (it == byId_.end()) return std::nullopt;
    return it->second;
}
ChannelId Recorder::addDerived(ChannelDesc desc, ChannelId source,
                               std::function<std::vector<double>(double, std::span<const double>)> fn) {
    ChannelId id = add(std::move(desc));
    std::lock_guard lk(mu_);
    entries_[source.get()].derived.emplace_back(id, std::move(fn));
    return id;
}
Status Recorder::push(ChannelId id, double x, std::span<const double> y) {
    std::vector<std::pair<ChannelId, std::vector<double>>> cascade;
    {
        std::lock_guard lk(mu_);
        if (frozen_) return fail(ErrorCode::Data_ + 1, "recorder is frozen");
        if (id.get() >= entries_.size()) return fail(ErrorCode::Data_ + 2, "unknown channel");
        Entry& e = entries_[id.get()];
        if (y.size() != e.y.size())
            return fail(ErrorCode::Data_ + 3, std::format("channel '{}' arity {} but {} values pushed", e.desc.id, e.y.size(), y.size()));
        e.x.push_back(x);
        for (std::size_t k = 0; k < y.size(); ++k) e.y[k].push_back(y[k]);
        for (auto& [did, fn] : e.derived) cascade.emplace_back(did, fn(x, y));
    }
    for (auto& [did, vals] : cascade) { auto st = push(did, x, vals); if (!st) return st; }
    return {};
}
void Recorder::mark(ChannelId id, double x, std::string label) {
    std::lock_guard lk(mu_);
    if (id.get() < entries_.size()) entries_[id.get()].markers.push_back({x, std::move(label)});
}
void Recorder::reserve(ChannelId id, std::size_t n) {
    std::lock_guard lk(mu_);
    if (id.get() >= entries_.size()) return;
    auto& e = entries_[id.get()]; e.x.reserve(n); for (auto& c : e.y) c.reserve(n);
}
Series Recorder::view(ChannelId id) const {
    std::lock_guard lk(mu_);
    Series s;
    if (id.get() >= entries_.size()) return s;
    const Entry& e = entries_[id.get()];
    s.desc = e.desc; s.x = e.x; s.y = e.y; s.markers = e.markers;
    return s;
}
Series Recorder::decimated(ChannelId id, double x0, double x1, std::size_t maxPoints) const {
    return decimate(view(id), x0, x1, maxPoints);
}
std::vector<ChannelDesc> Recorder::channels() const {
    std::lock_guard lk(mu_);
    std::vector<ChannelDesc> v; v.reserve(entries_.size());
    for (auto& e : entries_) v.push_back(e.desc);
    return v;
}
std::size_t Recorder::size(ChannelId id) const {
    std::lock_guard lk(mu_);
    return id.get() < entries_.size() ? entries_[id.get()].x.size() : 0;
}
void Recorder::clear() {
    std::lock_guard lk(mu_);
    for (auto& e : entries_) { e.x.clear(); for (auto& c : e.y) c.clear(); e.markers.clear(); }
    frozen_ = false;
}

} // namespace qlab::data
