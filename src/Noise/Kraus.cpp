// Spec 08 §1 — Kraus construction, CPTP validation and Pauli-structure detection.
#include "Noise/Kraus.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Tensor.hpp"
#include <cmath>
#include <format>

namespace qlab::noise {
using num::Complex;
using num::Matrix;

std::string_view placementName(Placement p) {
    switch (p) {
    case Placement::Before:
        return "before";
    case Placement::After:
        return "after";
    case Placement::During:
        return "during";
    }
    return "?";
}

namespace {
constexpr std::uint32_t kMaxPauliArity = 4;  // 4^4 = 256 strings
constexpr std::uint32_t kMaxDetectArity = 3; // Pauli detection in make()

// max |Σ K†K − I| for the error message.
double traceDefect(const std::vector<Matrix>& ops, std::size_t d) {
    Matrix sum(d, d);
    for (const auto& K : ops)
        sum += num::matmul(num::adjoint(K), K);
    double worst = 0.0;
    for (std::size_t i = 0; i < d; ++i)
        for (std::size_t j = 0; j < d; ++j)
            worst = std::max(worst, std::abs(sum(i, j) - (i == j ? Complex(1, 0) : Complex(0, 0))));
    return worst;
}

// K = c·P for exactly one Pauli string P: returns its index and |c|².
bool matchPauli(const Matrix& K, const std::vector<Matrix>& paulis, std::uint32_t& index,
                double& weight) {
    const double d = static_cast<double>(K.rows);
    std::size_t hits = 0;
    Complex coeff{};
    for (std::size_t p = 0; p < paulis.size(); ++p) {
        Complex c = num::trace(num::matmul(paulis[p], K)) / d; // Pauli expansion coefficient
        if (std::abs(c) > 1e-13) {
            ++hits;
            coeff = c;
            index = static_cast<std::uint32_t>(p);
        }
    }
    if (hits != 1)
        return false;
    if (!num::approxEqual(K, num::scale(paulis[index], coeff), 1e-12))
        return false;
    weight = std::norm(coeff);
    return true;
}
} // namespace

std::size_t Kraus::dim() const {
    return num::ipow(levels, arity);
}

bool Kraus::isUnitary() const {
    return ops.size() == 1 && num::isUnitary(ops.front(), 1e-10);
}

bool Kraus::isIdentity() const {
    if (ops.size() != 1)
        return false;
    const Matrix& K = ops.front();
    const Complex c = K(0, 0);
    if (std::abs(std::abs(c) - 1.0) > 1e-12)
        return false;
    return num::approxEqual(K, num::scale(Matrix::identity(K.rows), c), 1e-12);
}

Status Kraus::validate(double tolAbs) const {
    if (ops.empty())
        return fail(err::BadDimensions, "Kraus set is empty");
    if (arity == 0 || levels < 2)
        return fail(err::BadDimensions, "a channel needs arity >= 1 and levels >= 2");
    const std::size_t d = dim();
    for (std::size_t k = 0; k < ops.size(); ++k)
        if (ops[k].rows != d || ops[k].cols != d)
            return fail(err::BadDimensions,
                        std::format("K_{} is {}x{}; {} target(s) of dimension {} need {}x{}", k,
                                    ops[k].rows, ops[k].cols, arity, levels, d, d));
    if (!num::isTracePreserving(ops, tolAbs))
        return fail(err::NotTracePreserving,
                    std::format("sum K^dagger K deviates from I by {:.3e} (tolerance {:.1e})",
                                traceDefect(ops, d), tolAbs));
    if (isPauli && (pauliWeights.size() != ops.size() || pauliIndices.size() != ops.size()))
        return fail(err::BadDimensions, "Pauli weights/indices do not match the operator count");
    return {};
}

Result<Kraus> Kraus::make(std::vector<Matrix> opsIn, std::uint32_t arity, std::uint32_t levels) {
    Kraus k;
    k.arity = arity;
    k.levels = levels;
    // Operators that are exactly zero carry nothing (e.g. (2.2) at p_th = 0 reduces to (2.1)).
    for (auto& K : opsIn)
        if (num::maxAbsNorm(K) > 0.0 || opsIn.size() == 1)
            k.ops.push_back(std::move(K));
    QXL_TRY(k.validate());
    if (levels == 2 && arity <= kMaxDetectArity) {
        std::vector<Matrix> paulis;
        const std::uint32_t count = static_cast<std::uint32_t>(num::ipow(4, arity));
        for (std::uint32_t p = 0; p < count; ++p)
            paulis.push_back(pauliStringMatrix(p, arity));
        k.isPauli = true;
        for (const auto& K : k.ops) {
            std::uint32_t index = 0;
            double weight = 0.0;
            if (!matchPauli(K, paulis, index, weight)) {
                k.isPauli = false;
                break;
            }
            k.pauliIndices.push_back(index);
            k.pauliWeights.push_back(weight);
        }
        if (!k.isPauli) {
            k.pauliIndices.clear();
            k.pauliWeights.clear();
        }
    }
    return k;
}

Result<Kraus> Kraus::fromPauliWeights(std::uint32_t arity, std::span<const double> weights) {
    if (arity == 0 || arity > kMaxPauliArity)
        return fail(err::InvalidParameter,
                    std::format("Pauli channels support 1..{} qubits, {} requested", kMaxPauliArity,
                                arity));
    const std::size_t count = num::ipow(4, arity);
    if (weights.size() != count)
        return fail(err::BadDimensions, std::format("{} Pauli weights given, 4^{} = {} expected",
                                                    weights.size(), arity, count));
    double total = 0.0;
    for (double w : weights) {
        if (!(w >= -1e-15) || !std::isfinite(w))
            return fail(err::InvalidParameter,
                        std::format("Pauli weight {} is not a probability", w));
        total += std::max(0.0, w);
    }
    if (std::abs(total - 1.0) > 1e-12)
        return fail(err::InvalidParameter,
                    std::format("Pauli weights sum to {:.15g}, expected 1", total));
    Kraus k;
    k.arity = arity;
    k.isPauli = true;
    for (std::uint32_t p = 0; p < count; ++p) {
        const double w = std::max(0.0, weights[p]) / total; // renormalise the 1e-12 slack away
        if (w == 0.0)
            continue;
        k.ops.push_back(num::scale(pauliStringMatrix(p, arity), Complex(std::sqrt(w), 0.0)));
        k.pauliWeights.push_back(w);
        k.pauliIndices.push_back(p);
    }
    QXL_TRY(k.validate());
    return k;
}

Result<Kraus> Kraus::fromUnitary(Matrix u, std::uint32_t arity, std::uint32_t levels) {
    if (!num::isUnitary(u, 1e-10))
        return fail(err::NotTracePreserving, "coherent-error operator is not unitary");
    std::vector<Matrix> ops;
    ops.push_back(std::move(u));
    return make(std::move(ops), arity, levels);
}

Matrix pauliMatrix(std::uint32_t code) {
    switch (code & 3u) {
    case 1:
        return num::pauli::X.toMatrix();
    case 2:
        return num::pauli::Y.toMatrix();
    case 3:
        return num::pauli::Z.toMatrix();
    default:
        return num::pauli::I.toMatrix();
    }
}

Matrix pauliStringMatrix(std::uint32_t index, std::uint32_t arity) {
    std::vector<Matrix> factors; // element k acts on targets[k] (least significant first)
    for (std::uint32_t k = 0; k < arity; ++k)
        factors.push_back(pauliMatrix((index >> (2 * k)) & 3u));
    return num::kronList(factors);
}

std::string pauliStringLabel(std::uint32_t index, std::uint32_t arity) {
    static constexpr char letters[] = {'I', 'X', 'Y', 'Z'};
    std::string s(arity, 'I');
    for (std::uint32_t k = 0; k < arity; ++k)
        s[arity - 1 - k] = letters[(index >> (2 * k)) & 3u];
    return s;
}

Result<std::uint32_t> pauliStringIndex(std::string_view letters) {
    if (letters.empty() || letters.size() > kMaxPauliArity)
        return fail(err::InvalidParameter, std::format("Pauli axis '{}' must name 1..{} targets",
                                                       letters, kMaxPauliArity));
    std::uint32_t index = 0;
    for (std::size_t k = 0; k < letters.size(); ++k) {
        std::uint32_t code = 0;
        switch (letters[k]) {
        case 'I':
            code = 0;
            break;
        case 'X':
            code = 1;
            break;
        case 'Y':
            code = 2;
            break;
        case 'Z':
            code = 3;
            break;
        default:
            return fail(err::InvalidParameter,
                        std::format("invalid Pauli letter '{}' in axis '{}'", letters[k], letters));
        }
        index |= code << (2 * k);
    }
    return index;
}

} // namespace qlab::noise
