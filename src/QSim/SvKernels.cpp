// Spec 07 §2.2–2.3, T11 §2 — state-vector gate kernels. All indices little-endian.
#include "Core/JobSystem.hpp"
#include "QSim/StateVector.hpp"
#include <algorithm>
#include <bit>

namespace qlab::qsim {
using num::Mat2;
using num::Mat4;

void forRange(std::size_t count, const std::function<void(std::size_t, std::size_t)>& fn) {
    constexpr std::size_t grain = std::size_t{1} << 14;
    if (count < (std::size_t{1} << 16)) {
        fn(0, count);
        return;
    }
    core::JobSystem::global().parallelFor(count, grain, fn);
}

namespace {
// Insert a zero bit at position t into `x` (x has n-1 bits): index with bit t clear.
inline std::uint64_t insertZero(std::uint64_t x, std::uint32_t t) {
    std::uint64_t lo = x & ((std::uint64_t{1} << t) - 1);
    return ((x >> t) << (t + 1)) | lo;
}
inline bool ctrlOk(std::uint64_t i, std::uint64_t mask) {
    return (i & mask) == mask;
}
} // namespace

void StateVectorBackend::kernel1(std::span<Complex> psi, std::uint32_t /*n*/, std::uint32_t t,
                                 const Mat2& u, std::uint64_t ctrl) {
    const std::uint64_t m = std::uint64_t{1} << t;
    const std::size_t pairs = psi.size() / 2;
    const Complex a = u(0, 0), b = u(0, 1), c = u(1, 0), d = u(1, 1);
    forRange(pairs, [&](std::size_t b0, std::size_t e0) {
        for (std::size_t k = b0; k < e0; ++k) {
            std::uint64_t i = insertZero(k, t);
            if (ctrl && !ctrlOk(i, ctrl))
                continue;
            Complex x = psi[i], y = psi[i | m];
            psi[i] = a * x + b * y;
            psi[i | m] = c * x + d * y;
        }
    });
}

void StateVectorBackend::kernelDiag1(std::span<Complex> psi, std::uint32_t t, Complex d0,
                                     Complex d1, std::uint64_t ctrl) {
    const std::uint64_t m = std::uint64_t{1} << t;
    forRange(psi.size(), [&](std::size_t b0, std::size_t e0) {
        for (std::size_t i = b0; i < e0; ++i) {
            if (ctrl && !ctrlOk(i, ctrl))
                continue;
            psi[i] *= (i & m) ? d1 : d0;
        }
    });
}

void StateVectorBackend::kernelX(std::span<Complex> psi, std::uint32_t t, std::uint64_t ctrl) {
    const std::uint64_t m = std::uint64_t{1} << t;
    forRange(psi.size() / 2, [&](std::size_t b0, std::size_t e0) {
        for (std::size_t k = b0; k < e0; ++k) {
            std::uint64_t i = insertZero(k, t);
            if (ctrl && !ctrlOk(i, ctrl))
                continue;
            std::swap(psi[i], psi[i | m]);
        }
    });
}

void StateVectorBackend::kernelZ(std::span<Complex> psi, std::uint32_t t, std::uint64_t ctrl) {
    const std::uint64_t m = std::uint64_t{1} << t;
    forRange(psi.size(), [&](std::size_t b0, std::size_t e0) {
        for (std::size_t i = b0; i < e0; ++i)
            if ((i & m) && (!ctrl || ctrlOk(i, ctrl)))
                psi[i] = -psi[i];
    });
}

void StateVectorBackend::kernelCnot(std::span<Complex> psi, std::uint32_t c, std::uint32_t t,
                                    std::uint64_t ctrl) {
    kernelX(psi, t, ctrl | (std::uint64_t{1} << c));
}

void StateVectorBackend::kernelCz(std::span<Complex> psi, std::uint32_t a, std::uint32_t b,
                                  std::uint64_t ctrl) {
    kernelZ(psi, b, ctrl | (std::uint64_t{1} << a));
}

void StateVectorBackend::kernelSwap(std::span<Complex> psi, std::uint32_t t0, std::uint32_t t1,
                                    std::uint64_t ctrl) {
    const std::uint64_t m0 = std::uint64_t{1} << t0, m1 = std::uint64_t{1} << t1;
    forRange(psi.size(), [&](std::size_t b0, std::size_t e0) {
        for (std::size_t i = b0; i < e0; ++i) {
            if ((i & m0) && !(i & m1) && (!ctrl || ctrlOk(i, ctrl)))
                std::swap(psi[i], psi[(i ^ m0) | m1]);
        }
    });
}

void StateVectorBackend::kernel2(std::span<Complex> psi, std::uint32_t /*n*/, std::uint32_t t0,
                                 std::uint32_t t1, const Mat4& u, std::uint64_t ctrl) {
    // t0 = targets[0] (least significant of the matrix index), t1 = targets[1]. Need not be sorted.
    const std::uint64_t m0 = std::uint64_t{1} << t0, m1 = std::uint64_t{1} << t1;
    const std::uint32_t lo = std::min(t0, t1), hi = std::max(t0, t1);
    forRange(psi.size() / 4, [&](std::size_t b0, std::size_t e0) {
        for (std::size_t k = b0; k < e0; ++k) {
            std::uint64_t i = insertZero(insertZero(k, lo), hi);
            if (ctrl && !ctrlOk(i, ctrl))
                continue;
            Complex v[4] = {psi[i], psi[i | m0], psi[i | m1], psi[i | m0 | m1]};
            Complex r[4];
            for (int a = 0; a < 4; ++a)
                r[a] = u(a, 0) * v[0] + u(a, 1) * v[1] + u(a, 2) * v[2] + u(a, 3) * v[3];
            psi[i] = r[0];
            psi[i | m0] = r[1];
            psi[i | m1] = r[2];
            psi[i | m0 | m1] = r[3];
        }
    });
}

void StateVectorBackend::kernelDiag2(std::span<Complex> psi, std::uint32_t t0, std::uint32_t t1,
                                     const Mat4& u, std::uint64_t ctrl) {
    const std::uint64_t m0 = std::uint64_t{1} << t0, m1 = std::uint64_t{1} << t1;
    const Complex d[4] = {u(0, 0), u(1, 1), u(2, 2), u(3, 3)};
    forRange(psi.size(), [&](std::size_t b0, std::size_t e0) {
        for (std::size_t i = b0; i < e0; ++i) {
            if (ctrl && !ctrlOk(i, ctrl))
                continue;
            psi[i] *= d[((i & m0) ? 1 : 0) | ((i & m1) ? 2 : 0)];
        }
    });
}

void StateVectorBackend::kernelK(std::span<Complex> psi, std::uint32_t /*n*/,
                                 std::span<const std::uint32_t> targets, const Matrix& u,
                                 std::uint64_t ctrl) {
    const std::size_t k = targets.size();
    const std::size_t dim = std::size_t{1} << k;
    std::vector<std::uint32_t> sorted(targets.begin(), targets.end());
    std::sort(sorted.begin(), sorted.end());
    std::vector<std::uint64_t> masks(k);
    for (std::size_t j = 0; j < k; ++j)
        masks[j] = std::uint64_t{1} << targets[j];
    forRange(psi.size() >> k, [&](std::size_t b0, std::size_t e0) {
        std::vector<Complex> v(dim), r(dim);
        for (std::size_t g = b0; g < e0; ++g) {
            std::uint64_t i = g;
            for (auto t : sorted)
                i = insertZero(i, t);
            if (ctrl && !ctrlOk(i, ctrl))
                continue;
            for (std::size_t s = 0; s < dim; ++s) {
                std::uint64_t idx = i;
                for (std::size_t j = 0; j < k; ++j)
                    if (s & (std::size_t{1} << j))
                        idx |= masks[j];
                v[s] = psi[idx];
            }
            for (std::size_t a = 0; a < dim; ++a) {
                Complex acc{};
                for (std::size_t s = 0; s < dim; ++s)
                    acc += u(a, s) * v[s];
                r[a] = acc;
            }
            for (std::size_t s = 0; s < dim; ++s) {
                std::uint64_t idx = i;
                for (std::size_t j = 0; j < k; ++j)
                    if (s & (std::size_t{1} << j))
                        idx |= masks[j];
                psi[idx] = r[s];
            }
        }
    });
}

} // namespace qlab::qsim
