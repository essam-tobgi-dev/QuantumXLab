#include "Numerics/Tensor.hpp"
#include <algorithm>
#include <numeric>
namespace qlab::num {

std::size_t ipow(std::size_t b, std::size_t e) { std::size_t r = 1; while (e--) r *= b; return r; }

Matrix kron(ConstMatrixView a, ConstMatrixView b) {
    Matrix r(a.rows * b.rows, a.cols * b.cols);
    for (std::size_t i1 = 0; i1 < a.rows; ++i1)
        for (std::size_t j1 = 0; j1 < a.cols; ++j1) {
            Complex av = a(i1, j1);
            if (av == 0.0) continue;
            for (std::size_t i2 = 0; i2 < b.rows; ++i2)
                for (std::size_t j2 = 0; j2 < b.cols; ++j2)
                    r(i1 * b.rows + i2, j1 * b.cols + j2) = av * b(i2, j2);
        }
    return r;
}
Matrix kronList(std::span<const Matrix> ops) {
    if (ops.empty()) return Matrix::identity(1);
    Matrix acc = ops[0];
    for (std::size_t k = 1; k < ops.size(); ++k) acc = kron(ops[k], acc);
    return acc;
}
Vector kronVec(std::span<const Complex> a, std::span<const Complex> b) {
    // a = subsystem 0 (least significant), b = subsystem 1: index = ib*|a| + ia
    Vector r(a.size() * b.size());
    for (std::size_t ib = 0; ib < b.size(); ++ib)
        for (std::size_t ia = 0; ia < a.size(); ++ia) r[ib * a.size() + ia] = a[ia] * b[ib];
    return r;
}

namespace {
struct Strides {
    std::vector<std::size_t> stride; std::size_t total = 1;
    explicit Strides(std::span<const std::size_t> dims) : stride(dims.size()) {
        for (std::size_t k = 0; k < dims.size(); ++k) { stride[k] = total; total *= dims[k]; }
    }
};
} // namespace

Matrix partialTrace(ConstMatrixView rho, std::span<const std::size_t> dims, std::span<const std::size_t> keep) {
    Strides st(dims);
    assert(rho.rows == st.total && rho.cols == st.total);
    std::vector<std::size_t> traced;
    for (std::size_t k = 0; k < dims.size(); ++k)
        if (std::find(keep.begin(), keep.end(), k) == keep.end()) traced.push_back(k);
    std::size_t dimKeep = 1; for (auto k : keep) dimKeep *= dims[k];
    std::size_t dimTr = 1; for (auto k : traced) dimTr *= dims[k];
    Matrix out(dimKeep, dimKeep);
    // Build full index from (kept multi-index, traced multi-index).
    auto fullIndex = [&](std::size_t ik, std::size_t it) {
        std::size_t idx = 0;
        for (auto k : keep) { idx += (ik % dims[k]) * st.stride[k]; ik /= dims[k]; }
        for (auto k : traced) { idx += (it % dims[k]) * st.stride[k]; it /= dims[k]; }
        return idx;
    };
    for (std::size_t i = 0; i < dimKeep; ++i)
        for (std::size_t j = 0; j < dimKeep; ++j) {
            Complex s{};
            for (std::size_t t = 0; t < dimTr; ++t) s += rho(fullIndex(i, t), fullIndex(j, t));
            out(i, j) = s;
        }
    return out;
}

Vector permuteAxes(std::span<const Complex> psi, std::span<const std::size_t> dims, std::span<const std::size_t> perm) {
    Strides st(dims);
    assert(psi.size() == st.total && perm.size() == dims.size());
    std::vector<std::size_t> newDims(dims.size());
    for (std::size_t k = 0; k < perm.size(); ++k) newDims[k] = dims[perm[k]];
    Strides nst(newDims);
    Vector out(psi.size());
    std::vector<std::size_t> idx(dims.size());
    for (std::size_t i = 0; i < psi.size(); ++i) {
        std::size_t r = i;
        for (std::size_t k = 0; k < dims.size(); ++k) { idx[k] = r % dims[k]; r /= dims[k]; }
        std::size_t j = 0;
        for (std::size_t k = 0; k < perm.size(); ++k) j += idx[perm[k]] * nst.stride[k];
        out[j] = psi[i];
    }
    return out;
}

Matrix embed(ConstMatrixView U, std::span<const std::size_t> targets, std::size_t nQubits, std::size_t d) {
    const std::size_t k = targets.size();
    const std::size_t dk = ipow(d, k), N = ipow(d, nQubits);
    assert(U.rows == dk && U.cols == dk);
    std::vector<std::size_t> stride(nQubits);
    { std::size_t s = 1; for (std::size_t q = 0; q < nQubits; ++q) { stride[q] = s; s *= d; } }
    Matrix M(N, N);
    for (std::size_t i = 0; i < N; ++i) {
        // local input index and the "rest" (i with target digits zeroed)
        std::size_t li = 0, rest = i, lstride = 1;
        for (std::size_t t = 0; t < k; ++t) {
            std::size_t digit = (i / stride[targets[t]]) % d;
            li += digit * lstride; lstride *= d;
            rest -= digit * stride[targets[t]];
        }
        for (std::size_t lo = 0; lo < dk; ++lo) {
            Complex u = U(lo, li);
            if (u == 0.0) continue;
            std::size_t j = rest, r = lo;
            for (std::size_t t = 0; t < k; ++t) { j += (r % d) * stride[targets[t]]; r /= d; }
            M(j, i) += u;
        }
    }
    return M;
}

ConstMatrixView reshape(std::span<const Complex> v, std::size_t rows, std::size_t cols) { assert(v.size() == rows * cols); return ConstMatrixView(v.data(), rows, cols); }
MatrixView reshape(std::span<Complex> v, std::size_t rows, std::size_t cols) { assert(v.size() == rows * cols); return MatrixView(v.data(), rows, cols); }

Matrix reducedState(std::span<const Complex> psi, std::size_t n, std::span<const std::size_t> keep, std::size_t d) {
    std::vector<std::size_t> traced;
    for (std::size_t q = 0; q < n; ++q) if (std::find(keep.begin(), keep.end(), q) == keep.end()) traced.push_back(q);
    std::vector<std::size_t> stride(n);
    { std::size_t s = 1; for (std::size_t q = 0; q < n; ++q) { stride[q] = s; s *= d; } }
    std::size_t dimKeep = ipow(d, keep.size()), dimTr = ipow(d, traced.size());
    auto fullIndex = [&](std::size_t ik, std::size_t it) {
        std::size_t idx = 0;
        for (auto q : keep) { idx += (ik % d) * stride[q]; ik /= d; }
        for (auto q : traced) { idx += (it % d) * stride[q]; it /= d; }
        return idx;
    };
    Matrix rho(dimKeep, dimKeep);
    for (std::size_t t = 0; t < dimTr; ++t)
        for (std::size_t i = 0; i < dimKeep; ++i) {
            Complex a = psi[fullIndex(i, t)];
            if (a == 0.0) continue;
            for (std::size_t j = 0; j < dimKeep; ++j) rho(i, j) += a * std::conj(psi[fullIndex(j, t)]);
        }
    return rho;
}
} // namespace qlab::num
