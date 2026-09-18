// Spec 08 §3 — construction of the assignment map and its application to bits, counts and
// probability vectors.
#include "Noise/Readout.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::noise {
namespace {
constexpr std::size_t kMaxProbabilityBits = 24;

std::size_t localIndex(std::span<const std::uint8_t> bits, const ReadoutFactor& f) {
    std::size_t i = 0;
    for (std::size_t k = 0; k < f.positions.size(); ++k) i |= static_cast<std::size_t>(bits[f.positions[k]] & 1u) << k;
    return i;
}
} // namespace

Status validateAssignment(const num::RealMatrix& m, std::string_view what) {
    if (m.rows == 0 || m.rows != m.cols) return fail(err::BadReadout, std::format("{}: assignment matrix must be square", what));
    for (std::size_t i = 0; i < m.rows; ++i) {
        double sum = 0.0;
        for (std::size_t j = 0; j < m.cols; ++j) {
            const double v = m(i, j);
            if (!(v >= 0.0 && v <= 1.0)) return fail(err::BadReadout, std::format("{}: entry ({}, {}) = {} is not a probability", what, i, j, v));
            sum += v;
        }
        if (std::abs(sum - 1.0) > 1e-6) return fail(err::BadReadout, std::format("{}: row {} sums to {:.9g}, not 1 (row-stochastic, spec 08 §3)", what, i, sum));
    }
    return {};
}

num::RealMatrix toRealMatrix(const Assignment2& a) {
    num::RealMatrix m(2, 2);
    for (std::size_t i = 0; i < 2; ++i) for (std::size_t j = 0; j < 2; ++j) m(i, j) = a[i][j];
    return m;
}

double readoutFidelity(const Assignment2& a) { return 1.0 - 0.5 * (a[0][1] + a[1][0]); }

Result<ReadoutModel> ReadoutModel::make(std::vector<QubitIndex> qubits, std::vector<ReadoutFactor> factors) {
    std::vector<int> covered(qubits.size(), 0);
    for (std::size_t n = 0; n < factors.size(); ++n) {
        const auto& f = factors[n];
        const std::size_t k = f.positions.size();
        if (k == 0 || k > 4) return fail(err::BadReadout, std::format("readout factor {} spans {} qubits; groups hold 1..4 (spec 08 §3)", n, k));
        if (f.matrix.rows != (std::size_t{1} << k)) return fail(err::BadReadout, std::format("readout factor {} needs a {}x{} matrix", n, 1u << k, 1u << k));
        QXL_TRY(validateAssignment(f.matrix, std::format("readout factor {}", n)));
        for (auto pos : f.positions) {
            if (pos >= qubits.size()) return fail(err::BadReadout, std::format("readout factor {} refers to position {} of {} measured qubits", n, pos, qubits.size()));
            ++covered[pos];
        }
    }
    for (std::size_t k = 0; k < covered.size(); ++k)
        if (covered[k] != 1) return fail(err::BadReadout, std::format("measured qubit {} is covered by {} readout factors, expected 1", qubits[k].get(), covered[k]));
    ReadoutModel m;
    m.qubits_ = std::move(qubits);
    m.factors_ = std::move(factors);
    return m;
}

Result<ReadoutModel> ReadoutModel::independent(std::vector<QubitIndex> qubits, std::span<const Assignment2> assignment) {
    if (assignment.size() != qubits.size()) return fail(err::BadReadout, std::format("{} assignment matrices for {} qubits", assignment.size(), qubits.size()));
    std::vector<ReadoutFactor> factors;
    for (std::size_t k = 0; k < qubits.size(); ++k) factors.push_back({{k}, toRealMatrix(assignment[k])});
    return make(std::move(qubits), std::move(factors));
}

ReadoutModel ReadoutModel::ideal(std::vector<QubitIndex> qubits) {
    ReadoutModel m;
    for (std::size_t k = 0; k < qubits.size(); ++k) m.factors_.push_back({{k}, num::RealMatrix::identity(2)});
    m.qubits_ = std::move(qubits);
    return m;
}

bool ReadoutModel::isIdeal() const {
    for (const auto& f : factors_)
        for (std::size_t i = 0; i < f.matrix.rows; ++i)
            if (f.matrix(i, i) != 1.0) return false;
    return true;
}

Status ReadoutModel::applyToBits(std::span<std::uint8_t> bits, core::Random& rng) const {
    if (bits.size() != qubits_.size()) return fail(err::BadReadout, std::format("{} bits for {} measured qubits", bits.size(), qubits_.size()));
    for (const auto& f : factors_) {
        const std::size_t i = localIndex(bits, f), dim = f.matrix.rows;
        const double r = rng.uniform();
        double acc = 0.0;
        std::size_t j = dim - 1; // rounding slack falls into the last outcome
        for (std::size_t c = 0; c < dim; ++c) {
            acc += f.matrix(i, c);
            if (r < acc) { j = c; break; }
        }
        for (std::size_t k = 0; k < f.positions.size(); ++k) bits[f.positions[k]] = static_cast<std::uint8_t>((j >> k) & 1u);
    }
    return {};
}

Result<std::size_t> ReadoutModel::applyToIndex(std::size_t prepared, core::Random& rng) const {
    std::vector<std::uint8_t> bits(qubits_.size());
    for (std::size_t k = 0; k < bits.size(); ++k) bits[k] = static_cast<std::uint8_t>((prepared >> k) & 1u);
    QXL_TRY(applyToBits(bits, rng));
    std::size_t out = 0;
    for (std::size_t k = 0; k < bits.size(); ++k) out |= static_cast<std::size_t>(bits[k]) << k;
    return out;
}

Result<qsim::Counts> ReadoutModel::applyToCounts(const qsim::Counts& ideal, core::Random& rng) const {
    const std::size_t n = qubits_.size();
    qsim::Counts out;
    std::vector<std::uint8_t> bits(n);
    // Keys in sorted order: the random stream is consumed identically on every standard library.
    std::vector<std::pair<std::string, std::uint64_t>> sorted(ideal.begin(), ideal.end());
    std::sort(sorted.begin(), sorted.end());
    for (const auto& [key, count] : sorted) {
        if (key.size() != n) return fail(err::BadReadout, std::format("count key '{}' has {} bits, {} qubits are measured", key, key.size(), n));
        for (std::uint64_t s = 0; s < count; ++s) {
            for (std::size_t k = 0; k < n; ++k) bits[k] = key[n - 1 - k] == '1' ? 1 : 0; // MSB-first key
            QXL_TRY(applyToBits(bits, rng));
            ++out[qsim::bitsToKey(bits)];
        }
    }
    return out;
}

Status ReadoutModel::checkLength(std::size_t n) const {
    if (qubits_.size() > kMaxProbabilityBits) return fail(err::InvalidParameter, std::format("probability vectors are limited to {} measured qubits", kMaxProbabilityBits));
    if (n != (std::size_t{1} << qubits_.size())) return fail(err::BadReadout, std::format("distribution has {} entries, 2^{} expected", n, qubits_.size()));
    return {};
}

void ReadoutModel::applyAlong(std::vector<double>& v, const ReadoutFactor& f, const num::RealMatrix& b) const {
    const std::size_t dim = b.rows;
    std::size_t mask = 0;
    std::vector<std::size_t> offset(dim, 0);
    for (std::size_t k = 0; k < f.positions.size(); ++k) mask |= std::size_t{1} << f.positions[k];
    for (std::size_t l = 0; l < dim; ++l)
        for (std::size_t k = 0; k < f.positions.size(); ++k)
            if ((l >> k) & 1u) offset[l] |= std::size_t{1} << f.positions[k];
    std::vector<double> in(dim);
    for (std::size_t base = 0; base < v.size(); ++base) {
        if (base & mask) continue;
        for (std::size_t l = 0; l < dim; ++l) in[l] = v[base | offset[l]];
        for (std::size_t a = 0; a < dim; ++a) {
            double acc = 0.0;
            for (std::size_t l = 0; l < dim; ++l) acc += b(a, l) * in[l];
            v[base | offset[a]] = acc;
        }
    }
}

Result<std::vector<double>> ReadoutModel::applyToProbabilities(std::span<const double> ideal) const {
    QXL_TRY(checkLength(ideal.size()));
    std::vector<double> q(ideal.begin(), ideal.end());
    for (const auto& f : factors_) {
        num::RealMatrix mt(f.matrix.rows, f.matrix.cols); // q_j = Σ_i M_ij p_i  →  B = Mᵀ
        for (std::size_t i = 0; i < mt.rows; ++i) for (std::size_t j = 0; j < mt.cols; ++j) mt(j, i) = f.matrix(i, j);
        applyAlong(q, f, mt);
    }
    return q;
}

Result<num::RealMatrix> ReadoutModel::fullMatrix() const {
    const std::size_t n = qubits_.size();
    if (n > 10) return fail(err::InvalidParameter, "dense assignment matrices are limited to 10 qubits");
    const std::size_t dim = std::size_t{1} << n;
    num::RealMatrix m(dim, dim);
    for (std::size_t i = 0; i < dim; ++i) { // row i = image of the basis distribution e_i
        std::vector<double> e(dim, 0.0);
        e[i] = 1.0;
        QXL_TRY_ASSIGN(auto row, applyToProbabilities(e));
        for (std::size_t j = 0; j < dim; ++j) m(i, j) = row[j];
    }
    return m;
}

} // namespace qlab::noise
