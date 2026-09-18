#include "Data/Histogram.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::data {

Interval wilson(std::uint64_t k, std::uint64_t n, double z) {
    if (n == 0) return {0.0, 1.0, 0.5};
    double nn = static_cast<double>(n), p = static_cast<double>(k) / nn;
    double z2 = z * z;
    double denom = 1.0 + z2 / nn;
    double center = (p + z2 / (2.0 * nn)) / denom;
    double half = z / denom * std::sqrt(p * (1.0 - p) / nn + z2 / (4.0 * nn * nn));
    return {std::max(0.0, center - half), std::min(1.0, center + half), center};
}
double binomialSigma(std::uint64_t k, std::uint64_t n) {
    if (n == 0) return 1.0;
    double nn = static_cast<double>(n);
    double p = std::clamp(static_cast<double>(k) / nn, 0.5 / nn, 1.0 - 0.5 / nn);
    return std::sqrt(p * (1.0 - p) / nn);
}

std::string Histogram::labelFromIndex(std::uint64_t index, std::size_t nbits) {
    std::string s(nbits, '0');
    for (std::size_t k = 0; k < nbits; ++k)
        if (index & (1ull << k)) s[nbits - 1 - k] = '1';
    return s;
}
std::uint64_t Histogram::indexFromLabel(const std::string& label) {
    std::uint64_t v = 0;
    std::size_t n = label.size();
    for (std::size_t k = 0; k < n; ++k)
        if (label[n - 1 - k] == '1') v |= (1ull << k);
    return v;
}
void Histogram::add(const std::string& label, std::uint64_t count) {
    if (nbits_ == 0) nbits_ = label.size();
    counts_[label] += count;
    total_ += count;
}
void Histogram::add(std::uint64_t index, std::uint64_t count) { add(labelFromIndex(index, nbits_), count); }
std::uint64_t Histogram::count(const std::string& label) const {
    auto it = counts_.find(label);
    return it == counts_.end() ? 0 : it->second;
}
double Histogram::probability(const std::string& label) const {
    return total_ == 0 ? 0.0 : static_cast<double>(count(label)) / static_cast<double>(total_);
}
std::vector<std::pair<std::string, std::uint64_t>> Histogram::all() const {
    return {counts_.begin(), counts_.end()};
}
std::vector<std::pair<std::string, std::uint64_t>> Histogram::topK(std::size_t k) const {
    auto v = all();
    std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
    if (v.size() > k) v.resize(k);
    return v;
}
Histogram Histogram::marginal(std::span<const std::size_t> qubits) const {
    Histogram m(qubits.size());
    m.cls = cls;
    for (auto& [label, c] : counts_) {
        std::size_t n = label.size();
        std::string out(qubits.size(), '0');
        // Output label convention: position j (from the right, i.e. output qubit j) is input qubit qubits[j].
        for (std::size_t j = 0; j < qubits.size(); ++j) {
            std::size_t q = qubits[j];
            char bit = q < n ? label[n - 1 - q] : '0';
            out[qubits.size() - 1 - j] = bit;
        }
        m.add(out, c);
    }
    return m;
}

Histogram1D Histogram1D::build(std::span<const double> values, std::size_t bins) {
    Histogram1D h;
    if (values.empty()) return h;
    std::vector<double> v(values.begin(), values.end());
    std::sort(v.begin(), v.end());
    double lo = v.front(), hi = v.back();
    if (bins == 0) {
        std::size_t n = v.size();
        double q1 = v[n / 4], q3 = v[(3 * n) / 4];
        double iqr = q3 - q1;
        double w = 2.0 * iqr * std::pow(static_cast<double>(n), -1.0 / 3.0);
        if (w <= 0 || hi <= lo) bins = 1;
        else bins = std::clamp<std::size_t>(static_cast<std::size_t>(std::ceil((hi - lo) / w)), 1, 4096);
    }
    if (hi <= lo) { hi = lo + 1.0; }
    h.edges.resize(bins + 1);
    for (std::size_t i = 0; i <= bins; ++i) h.edges[i] = lo + (hi - lo) * static_cast<double>(i) / static_cast<double>(bins);
    h.counts.assign(bins, 0);
    for (double x : v) {
        std::size_t b = static_cast<std::size_t>((x - lo) / (hi - lo) * static_cast<double>(bins));
        if (b >= bins) b = bins - 1;
        ++h.counts[b];
    }
    h.total = v.size();
    return h;
}

Histogram2D Histogram2D::build(std::span<const double> xs, std::span<const double> ys, std::size_t nx, std::size_t ny,
                               std::optional<std::array<double, 4>> range) {
    Histogram2D h; h.nx = nx; h.ny = ny; h.counts.assign(nx * ny, 0);
    if (xs.empty() || xs.size() != ys.size()) return h;
    if (range) { h.x0 = (*range)[0]; h.x1 = (*range)[1]; h.y0 = (*range)[2]; h.y1 = (*range)[3]; }
    else {
        auto [xmin, xmax] = std::minmax_element(xs.begin(), xs.end());
        auto [ymin, ymax] = std::minmax_element(ys.begin(), ys.end());
        h.x0 = *xmin; h.x1 = *xmax; h.y0 = *ymin; h.y1 = *ymax;
        if (h.x1 <= h.x0) h.x1 = h.x0 + 1.0;
        if (h.y1 <= h.y0) h.y1 = h.y0 + 1.0;
    }
    for (std::size_t i = 0; i < xs.size(); ++i) {
        double fx = (xs[i] - h.x0) / (h.x1 - h.x0), fy = (ys[i] - h.y0) / (h.y1 - h.y0);
        if (fx < 0 || fx > 1 || fy < 0 || fy > 1) continue;
        std::size_t ix = std::min(nx - 1, static_cast<std::size_t>(fx * static_cast<double>(nx)));
        std::size_t iy = std::min(ny - 1, static_cast<std::size_t>(fy * static_cast<double>(ny)));
        ++h.counts[iy * nx + ix];
        ++h.total;
    }
    return h;
}

} // namespace qlab::data
