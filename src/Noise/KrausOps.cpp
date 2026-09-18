// T04 §1.4, §5.3 and T10 §1 — channel algebra: composition, tensoring, level expansion, Pauli twirl
// and the fidelity figures used by the calibration mapping (spec 08 §4.1).
#include "Noise/Kraus.hpp"
#include "Numerics/Tensor.hpp"
#include <cmath>
#include <format>

namespace qlab::noise {
using num::Complex;
using num::Matrix;

Result<Kraus> compose(const Kraus& first, const Kraus& second) {
    if (first.arity != second.arity || first.levels != second.levels)
        return fail(err::BadDimensions, "composed channels must act on the same targets and levels");
    if (first.isPauli && second.isPauli) {
        // Pauli products: the code of P·Q is the XOR of the codes (phases drop out of PρP†).
        std::vector<double> w(num::ipow(4, first.arity), 0.0);
        for (std::size_t a = 0; a < first.ops.size(); ++a)
            for (std::size_t b = 0; b < second.ops.size(); ++b)
                w[first.pauliIndices[a] ^ second.pauliIndices[b]] += first.pauliWeights[a] * second.pauliWeights[b];
        return Kraus::fromPauliWeights(first.arity, w);
    }
    std::vector<Matrix> ops;
    for (const auto& B : second.ops)
        for (const auto& A : first.ops) {
            Matrix prod = num::matmul(B, A);
            if (num::maxAbsNorm(prod) > 0.0) ops.push_back(std::move(prod));
        }
    if (ops.empty()) return fail(err::NotTracePreserving, "composition produced no operators");
    return Kraus::make(std::move(ops), first.arity, first.levels);
}

Result<Kraus> tensor(const Kraus& low, const Kraus& high) {
    if (low.levels != high.levels) return fail(err::BadDimensions, "tensored channels must share the site dimension");
    const std::uint32_t arity = low.arity + high.arity;
    if (low.isPauli && high.isPauli && arity <= 4) {
        std::vector<double> w(num::ipow(4, arity), 0.0);
        for (std::size_t a = 0; a < low.ops.size(); ++a)
            for (std::size_t b = 0; b < high.ops.size(); ++b)
                w[low.pauliIndices[a] | (high.pauliIndices[b] << (2 * low.arity))] += low.pauliWeights[a] * high.pauliWeights[b];
        return Kraus::fromPauliWeights(arity, w);
    }
    std::vector<Matrix> ops;
    for (const auto& H : high.ops)
        for (const auto& L : low.ops) ops.push_back(num::kron(H, L)); // left factor = more significant
    return Kraus::make(std::move(ops), arity, low.levels);
}

Result<Kraus> expandLevels(const Kraus& k, std::uint32_t levels) {
    if (k.levels == levels) return k;
    if (k.levels != 2 || levels < 2)
        return fail(err::LevelsMismatch, std::format("cannot map a {}-level channel onto {}-level sites", k.levels, levels));
    const std::size_t dq = std::size_t{1} << k.arity, D = num::ipow(levels, k.arity);
    std::vector<std::size_t> siteIndex(dq); // computational basis state → mixed-radix index
    for (std::size_t b = 0; b < dq; ++b) {
        std::size_t idx = 0, stride = 1;
        for (std::uint32_t s = 0; s < k.arity; ++s) { idx += ((b >> s) & 1u) * stride; stride *= levels; }
        siteIndex[b] = idx;
    }
    std::vector<bool> computational(D, false);
    for (auto i : siteIndex) computational[i] = true;
    std::vector<Matrix> ops;
    for (std::size_t n = 0; n < k.ops.size(); ++n) {
        Matrix big(D, D);
        for (std::size_t a = 0; a < dq; ++a)
            for (std::size_t b = 0; b < dq; ++b) big(siteIndex[a], siteIndex[b]) = k.ops[n](a, b);
        if (n == 0) // leaked population is left untouched by a qubit channel
            for (std::size_t i = 0; i < D; ++i) if (!computational[i]) big(i, i) = 1.0;
        ops.push_back(std::move(big));
    }
    return Kraus::make(std::move(ops), k.arity, levels);
}

Result<PauliTwirl> pauliTwirl(const Kraus& k) {
    if (k.levels != 2) return fail(err::NotPauli, "a Pauli twirl exists only for qubit (d = 2) channels");
    if (k.arity == 0 || k.arity > 4) return fail(err::InvalidParameter, "Pauli twirl supports 1..4 qubits");
    const std::size_t count = num::ipow(4, k.arity);
    PauliTwirl t;
    t.arity = k.arity;
    t.probs.assign(count, 0.0);
    if (k.isPauli) {
        for (std::size_t n = 0; n < k.ops.size(); ++n) t.probs[k.pauliIndices[n]] += k.pauliWeights[n];
        return t;
    }
    // c[n][P] = Tr(P K_n)/d; χ_PQ = Σ_n c_nP conj(c_nQ). The twirl keeps the diagonal (T04 §5.3).
    const double d = static_cast<double>(k.dim());
    std::vector<std::vector<Complex>> c(k.ops.size(), std::vector<Complex>(count));
    for (std::size_t p = 0; p < count; ++p) {
        const Matrix P = pauliStringMatrix(static_cast<std::uint32_t>(p), k.arity);
        for (std::size_t n = 0; n < k.ops.size(); ++n) c[n][p] = num::trace(num::matmul(P, k.ops[n])) / d;
    }
    double offDiagonal = 0.0;
    for (std::size_t p = 0; p < count; ++p)
        for (std::size_t q = 0; q < count; ++q) {
            Complex chi{};
            for (std::size_t n = 0; n < k.ops.size(); ++n) chi += c[n][p] * std::conj(c[n][q]);
            if (p == q) t.probs[p] = chi.real();
            else offDiagonal = std::max(offDiagonal, std::abs(chi));
        }
    t.exact = offDiagonal <= 1e-12;
    return t;
}

double processFidelity(const Kraus& k, num::ConstMatrixView target) {
    const double d = static_cast<double>(k.dim());
    double sum = 0.0;
    if (target.rows == 0) {
        for (const auto& K : k.ops) sum += std::norm(num::trace(K));
    } else {
        const Matrix ud = num::adjoint(target);
        for (const auto& K : k.ops) sum += std::norm(num::trace(num::matmul(ud, K)));
    }
    return sum / (d * d);
}

double averageGateFidelity(const Kraus& k, num::ConstMatrixView target) {
    const double d = static_cast<double>(k.dim());
    return (d * processFidelity(k, target) + 1.0) / (d + 1.0);
}

double depolarizingEquivalent(const Kraus& k) {
    const double d = static_cast<double>(k.dim());
    return d / (d - 1.0) * (1.0 - averageGateFidelity(k));
}

Result<Matrix> applyToDensity(const Kraus& k, const Matrix& rho) {
    const std::size_t d = k.dim();
    if (rho.rows != d || rho.cols != d)
        return fail(err::BadDimensions, std::format("density matrix is {}x{}, channel acts on {}x{}", rho.rows, rho.cols, d, d));
    Matrix out(d, d);
    for (const auto& K : k.ops) out += num::matmul(num::matmul(K, rho), num::adjoint(K));
    return out;
}

} // namespace qlab::noise
