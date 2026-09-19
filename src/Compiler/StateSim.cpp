// Spec 14 §10 — state-vector kernel of the equivalence checker (little-endian, T01 §3).
#include "Compiler/StateSim.hpp"
#include <cmath>
#include <format>

namespace qlab::compiler::detail {

StateVec::StateVec(std::uint32_t qubits)
    : n_(qubits), amp_(std::size_t{1} << qubits, num::Complex{}) {
    amp_[0] = 1.0;
}

void StateVec::setBasis(std::size_t index) {
    std::fill(amp_.begin(), amp_.end(), num::Complex{});
    amp_[index] = 1.0;
}

void StateVec::apply(const num::Matrix& u, std::span<const std::uint32_t> wires, std::size_t cmask,
                     std::size_t cpat) {
    const std::size_t dim = amp_.size();
    num::Complex* a = amp_.data();
    if (wires.size() == 1) {
        const std::size_t bit = std::size_t{1} << wires[0];
        const num::Complex u00 = u(0, 0), u01 = u(0, 1), u10 = u(1, 0), u11 = u(1, 1);
        const bool diagonal = u01 == num::Complex{} && u10 == num::Complex{};
        for (std::size_t hi = 0; hi < dim; hi += 2 * bit)
            for (std::size_t lo = hi; lo < hi + bit; ++lo) {
                if ((lo & cmask) != cpat)
                    continue;
                if (diagonal) {
                    a[lo] *= u00;
                    a[lo | bit] *= u11;
                } else {
                    const num::Complex x = a[lo], y = a[lo | bit];
                    a[lo] = u00 * x + u01 * y;
                    a[lo | bit] = u10 * x + u11 * y;
                }
            }
        return;
    }
    const std::size_t k = wires.size(), block = std::size_t{1} << k;
    std::size_t targetMask = 0;
    std::vector<std::size_t> offset(block, 0);
    for (std::size_t t = 0; t < k; ++t)
        targetMask |= std::size_t{1} << wires[t];
    for (std::size_t m = 0; m < block; ++m)
        for (std::size_t t = 0; t < k; ++t)
            if ((m >> t) & 1u)
                offset[m] |= std::size_t{1} << wires[t];
    std::vector<num::Complex> in(block);
    for (std::size_t base = 0; base < dim; ++base) {
        if ((base & targetMask) != 0 || (base & cmask) != cpat)
            continue;
        for (std::size_t m = 0; m < block; ++m)
            in[m] = a[base | offset[m]];
        for (std::size_t r = 0; r < block; ++r) {
            num::Complex s{};
            for (std::size_t m = 0; m < block; ++m)
                s += u(r, m) * in[m];
            a[base | offset[r]] = s;
        }
    }
}

Status StateVec::applyGate(const ir::Gate& g, std::span<const std::uint32_t> wireMap) {
    QXL_TRY_ASSIGN(const num::Matrix u, ir::baseMatrixOf(g));
    std::vector<std::uint32_t> wires;
    std::size_t cmask = 0, cpat = 0;
    auto mapped = [&](ir::Wire w) -> Result<std::uint32_t> {
        if (w.index >= wireMap.size() || wireMap[w.index] >= n_)
            return fail(ErrorCode::OutOfRange,
                        std::format("gate '{}' uses wire {} outside the simulated register", g.name,
                                    w.index));
        return wireMap[w.index];
    };
    for (ir::Wire w : g.targets) {
        QXL_TRY_ASSIGN(const std::uint32_t q, mapped(w));
        wires.push_back(q);
    }
    for (std::size_t j = 0; j < g.controls.size(); ++j) {
        QXL_TRY_ASSIGN(const std::uint32_t q, mapped(g.controls[j]));
        cmask |= std::size_t{1} << q;
        if (!g.isNegControl(j))
            cpat |= std::size_t{1} << q;
    }
    apply(u, wires, cmask, cpat);
    return {};
}

double StateVec::probabilityOfOne(std::uint32_t wire) const {
    const std::size_t bit = std::size_t{1} << wire;
    double p = 0.0;
    for (std::size_t i = 0; i < amp_.size(); ++i)
        if (i & bit)
            p += std::norm(amp_[i]);
    return p;
}

double StateVec::project(std::uint32_t wire, int outcome) {
    const std::size_t bit = std::size_t{1} << wire;
    const double p1 = probabilityOfOne(wire);
    const double p = outcome ? p1 : 1.0 - p1;
    const double scale = p > 0.0 ? 1.0 / std::sqrt(p) : 0.0;
    for (std::size_t i = 0; i < amp_.size(); ++i) {
        if (((i & bit) != 0) == (outcome != 0))
            amp_[i] *= scale;
        else
            amp_[i] = 0.0;
    }
    return p;
}

void StateVec::flip(std::uint32_t wire) {
    const std::size_t bit = std::size_t{1} << wire;
    for (std::size_t i = 0; i < amp_.size(); ++i)
        if (!(i & bit))
            std::swap(amp_[i], amp_[i | bit]);
}

double StateVec::norm() const {
    double s = 0.0;
    for (const auto& z : amp_)
        s += std::norm(z);
    return std::sqrt(s);
}

std::vector<num::Complex> haarState(std::uint32_t qubits, core::Random& rng) {
    std::vector<num::Complex> psi(std::size_t{1} << qubits);
    double s = 0.0;
    for (auto& z : psi) {
        z = num::Complex(rng.normal(), rng.normal());
        s += std::norm(z);
    }
    const double scale = 1.0 / std::sqrt(s);
    for (auto& z : psi)
        z *= scale;
    return psi;
}

num::Complex overlap(std::span<const num::Complex> a, std::span<const num::Complex> b) {
    num::Complex s{};
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        s += std::conj(a[i]) * b[i];
    return s;
}

} // namespace qlab::compiler::detail
