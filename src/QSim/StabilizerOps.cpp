// Spec 07 §4 — Clifford recognition by conjugation images and gate application on the tableau.
#include "Numerics/Checks.hpp"
#include "Numerics/Tensor.hpp"
#include "QSim/Stabilizer.hpp"
#include <bit>
#include <format>

namespace qlab::qsim {
namespace {
// Multiply local Paulis in the convention i^phase X^x Z^z (bitwise, per-qubit X before Z).
LocalPauli mul(const LocalPauli& a, const LocalPauli& b) {
    LocalPauli r; r.x = a.x ^ b.x; r.z = a.z ^ b.z;
    r.phase = (a.phase + b.phase + 2 * std::popcount(a.z & b.x)) & 3;
    return r;
}
Matrix localMatrix(std::uint32_t x, std::uint32_t z, std::size_t k) {
    // Y-convention: (1,1) → Y. Little-endian kron: qubit 0 least significant.
    std::vector<Matrix> ops;
    for (std::size_t j = 0; j < k; ++j) {
        bool bx = (x >> j) & 1, bz = (z >> j) & 1;
        const num::Mat2& m = bx ? (bz ? num::pauli::Y : num::pauli::X) : (bz ? num::pauli::Z : num::pauli::I);
        ops.push_back(m.toMatrix());
    }
    return num::kronList(ops);
}
// Arity a GateClass fast path needs; 0 = no fast path on the tableau.
std::size_t fastPathArity(GateClass cls) {
    switch (cls) {
    case GateClass::PauliX: case GateClass::PauliZ: return 1;
    case GateClass::Cnot: case GateClass::Cz: case GateClass::Swap: return 2;
    default: return 0;
    }
}
} // namespace

Result<std::vector<LocalPauli>> StabilizerBackend::cliffordImages(const Matrix& u, std::size_t k) {
    const std::size_t dim = std::size_t{1} << k;
    if (u.rows != dim || u.cols != dim) return fail(err::BadTargets, "matrix dimension mismatch");
    if (k > 3) return fail(err::Unsupported, "Clifford recognition supports gates on ≤ 3 qubits");
    Matrix udag = num::adjoint(u);
    std::vector<LocalPauli> images;
    for (std::size_t j = 0; j < k; ++j) {
        for (int which = 0; which < 2; ++which) { // X_j then Z_j
            std::uint32_t gx = which == 0 ? (1u << j) : 0, gz = which == 0 ? 0 : (1u << j);
            Matrix m = num::matmul(num::matmul(u, localMatrix(gx, gz, k)), udag);
            // x of the image: the single non-zero column of row 0 (a Pauli is a phased permutation).
            std::uint32_t xi = 0; bool found = false;
            for (std::size_t c = 0; c < dim; ++c) if (std::abs(m(0, c)) > 1e-9) { if (found) return fail(err::NotClifford, "gate is not Clifford"); xi = static_cast<std::uint32_t>(c); found = true; }
            if (!found) return fail(err::NotClifford, "gate is not Clifford");
            // z of the image: try every candidate and compare the whole matrix up to a sign.
            bool matched = false;
            for (std::uint32_t zi = 0; zi < dim && !matched; ++zi) {
                Matrix p = localMatrix(xi, zi, k);
                Complex alpha = m(0, xi) / p(0, xi);
                if (std::abs(std::abs(alpha) - 1.0) > 1e-9) continue;
                bool ok = true;
                for (std::size_t r = 0; r < dim && ok; ++r) for (std::size_t c = 0; c < dim; ++c)
                    if (std::abs(m(r, c) - alpha * p(r, c)) > 1e-9) { ok = false; break; }
                if (!ok) continue;
                LocalPauli lp; lp.x = xi; lp.z = zi;
                std::uint32_t ph;
                if (std::abs(alpha - 1.0) < 1e-9) ph = 0; else if (std::abs(alpha + 1.0) < 1e-9) ph = 2;
                else return fail(err::NotClifford, "gate is not Clifford (non-Hermitian image)");
                lp.phase = (ph + std::popcount(xi & zi)) & 3;
                images.push_back(lp);
                matched = true;
            }
            if (!matched) return fail(err::NotClifford, "gate is not Clifford");
        }
    }
    return images;
}

void StabilizerBackend::applyImages(std::span<const std::uint32_t> t, std::span<const LocalPauli> img) {
    const std::size_t k = t.size();
    for (std::size_t row = 0; row < 2 * std::size_t(n_); ++row) {
        std::uint32_t x = 0, z = 0;
        for (std::size_t j = 0; j < k; ++j) { if (getX(row, t[j])) x |= 1u << j; if (getZ(row, t[j])) z |= 1u << j; }
        if (!x && !z) continue;
        LocalPauli acc; acc.phase = 0;
        for (std::size_t j = 0; j < k; ++j) {
            if ((x >> j) & 1) acc = mul(acc, img[2 * j]);
            if ((z >> j) & 1) acc = mul(acc, img[2 * j + 1]);
        }
        acc.phase = (acc.phase + std::popcount(x & z)) & 3;
        std::uint32_t rloc = (acc.phase + 4 - static_cast<std::uint32_t>(std::popcount(acc.x & acc.z))) & 3; // 0 or 2
        if (rloc == 2) r_[row] ^= 1;
        for (std::size_t j = 0; j < k; ++j) { setX(row, t[j], (acc.x >> j) & 1); setZ(row, t[j], (acc.z >> j) & 1); }
    }
}

Status StabilizerBackend::applyGate(const Matrix& u, std::span<const QubitIndex> targets) {
    if (!allocated_) return fail(err::NotAllocated, "backend not allocated");
    if (!validTargets(targets, n_)) return fail(err::BadTargets, "invalid targets");
    auto img = cliffordImages(u, targets.size());
    if (!img) return std::unexpected(img.error());
    std::vector<std::uint32_t> t(targets.size());
    for (std::size_t i = 0; i < t.size(); ++i) t[i] = targets[i].value;
    applyImages(t, *img);
    ++ops_;
    return {};
}

Status StabilizerBackend::applyControlled(const Matrix& u, std::span<const QubitIndex> controls, std::span<const QubitIndex> targets) {
    if (controls.size() + targets.size() > 3) return fail(err::Unsupported, "controlled Clifford recognition limited to 3 qubits total");
    const std::size_t kt = targets.size(), kc = controls.size();
    const std::size_t dim = std::size_t{1} << (kt + kc), dt = std::size_t{1} << kt;
    if (u.rows != dt || u.cols != dt) return fail(err::BadTargets, "matrix dimension does not match target count");
    // Build the controlled matrix with controls as the most significant indices.
    Matrix cu = Matrix::identity(dim);
    std::size_t allOn = ((std::size_t{1} << kc) - 1) << kt;
    for (std::size_t a = 0; a < dt; ++a) for (std::size_t b = 0; b < dt; ++b) cu(allOn | a, allOn | b) = u(a, b);
    std::vector<QubitIndex> all(targets.begin(), targets.end());
    all.insert(all.end(), controls.begin(), controls.end());
    return applyGate(cu, all);
}

Status StabilizerBackend::apply(const GateOp& op) {
    if (!allocated_) return fail(err::NotAllocated, "backend not allocated");
    const std::size_t arity = fastPathArity(op.cls);
    // Class fast paths only when the op is shaped like its class; anything else is recognised by matrix.
    if (op.controls.empty() && validTargets(op.targets, n_) &&
        (op.cls == GateClass::Identity || (arity != 0 && op.targets.size() == arity))) {
        const std::uint32_t t0 = op.targets.empty() ? 0u : op.targets[0].value;
        switch (op.cls) {
        case GateClass::Identity: return {};
        case GateClass::PauliX: x(t0); ++ops_; return {};
        case GateClass::PauliZ: z(t0); ++ops_; return {};
        case GateClass::Cnot: cnot(t0, op.targets[1].value); ++ops_; return {};
        case GateClass::Cz: cz(t0, op.targets[1].value); ++ops_; return {};
        case GateClass::Swap: swap(t0, op.targets[1].value); ++ops_; return {};
        default: break;
        }
    }
    Status r = op.controls.empty() ? applyGate(op.matrix, op.targets) : applyControlled(op.matrix, op.controls, op.targets);
    if (!r) r.error().notes.push_back(std::format("gate '{}' (op #{})", op.name, op.opIndex));
    return r;
}

} // namespace qlab::qsim
