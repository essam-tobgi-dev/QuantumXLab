// Spec 14 §10 — Clifford tableau by Pauli conjugation. Local Paulis are kept in XZ form
// i^r X^a Z^b; (a₁,b₁,r₁)·(a₂,b₂,r₂) = (a₁⊕a₂, b₁⊕b₂, r₁ + r₂ + 2|b₁∧a₂|) because Z X = −X Z.
#include "Compiler/Tableau.hpp"
#include "Numerics/Matrix.hpp"
#include <bit>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::compiler {
namespace {
constexpr double kTol = 1e-9;

// X^a Z^b on k local qubits: |s⟩ → (−1)^{b·s} |s ⊕ a⟩.
num::Matrix localPauli(std::size_t k, std::size_t a, std::size_t b) {
    const std::size_t dim = std::size_t{1} << k;
    num::Matrix m(dim, dim);
    for (std::size_t s = 0; s < dim; ++s) m(s ^ a, s) = (std::popcount(b & s) & 1) ? -1.0 : 1.0;
    return m;
}
} // namespace

std::string PauliString::text() const {
    int r = phase;
    std::string body;
    for (std::size_t q = x.size(); q-- > 0;) {
        if (x[q] && z[q]) { body += 'Y'; r = (r + 3) & 3; }   // X·Z = −i·Y
        else body += x[q] ? 'X' : z[q] ? 'Z' : 'I';
    }
    static constexpr const char* sign[4] = {"+", "+i", "-", "-i"};
    return sign[r & 3] + body;
}

CliffordTableau::CliffordTableau(std::uint32_t qubits) : n_(qubits), rows_(2 * static_cast<std::size_t>(qubits)) {
    for (std::uint32_t r = 0; r < 2 * n_; ++r) {
        rows_[r].x.assign(n_, 0);
        rows_[r].z.assign(n_, 0);
        (r < n_ ? rows_[r].x[r] : rows_[r].z[r - n_]) = 1;
    }
}

const CliffordTableau::GateTable* CliffordTableau::tableFor(const ir::Gate& g) {
    std::string key = std::format("{}|{}|{}|{}", g.name, g.adjoint ? 1 : 0, g.targets.size(), g.controls.size());
    for (std::size_t j = 0; j < g.controls.size(); ++j) key += g.isNegControl(j) ? 'n' : 'p';
    for (double p : g.params) key += std::format("|{:.17g}", p);
    if (g.custom) key += std::format("|custom{}", cache_.size());   // an explicit matrix is never shared
    if (auto it = cache_.find(key); it != cache_.end()) return it->second ? &*it->second : nullptr;

    std::optional<GateTable> table;
    const std::size_t k = g.width();
    auto full = ir::matrixOf(g);
    if (full && k >= 1 && k <= 3 && full->rows == (std::size_t{1} << k)) {
        const std::size_t dim = full->rows;
        const num::Matrix dagger = num::adjoint(full->view());
        GateTable t;
        bool clifford = true;
        for (std::size_t j = 0; j < k && clifford; ++j)
            for (int which = 0; which < 2 && clifford; ++which) {   // 0: X_j, 1: Z_j
                const num::Matrix gen = which == 0 ? localPauli(k, std::size_t{1} << j, 0) : localPauli(k, 0, std::size_t{1} << j);
                const num::Matrix image = num::matmul(num::matmul(*full, gen), dagger);
                bool found = false;
                for (std::size_t a = 0; a < dim && !found; ++a)
                    for (std::size_t b = 0; b < dim && !found; ++b) {
                        num::Complex c{};
                        for (std::size_t s = 0; s < dim; ++s) c += ((std::popcount(b & s) & 1) ? -1.0 : 1.0) * image(s ^ a, s);
                        c /= static_cast<double>(dim);
                        if (std::abs(c) < 0.5) continue;
                        const long quarter = std::lround(std::arg(c) / (std::numbers::pi / 2.0));
                        const auto r = static_cast<std::uint8_t>(((quarter % 4) + 4) % 4);
                        if (std::abs(c - std::polar(1.0, r * std::numbers::pi / 2.0)) > kTol) { clifford = false; break; }
                        (which == 0 ? t.imageX : t.imageZ).push_back({static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b), r});
                        found = true;
                    }
                clifford = clifford && found;
            }
        if (clifford) table = std::move(t);
    }
    const auto it = cache_.emplace(std::move(key), std::move(table)).first;
    return it->second ? &*it->second : nullptr;
}

bool CliffordTableau::apply(const ir::Gate& g) {
    if (g.opaque) return false;
    if (g.width() == 0) return true;   // gphase: no action on the Pauli group
    const auto wires = g.wires();
    for (ir::Wire w : wires)
        if (w.index >= n_) return false;
    const GateTable* t = tableFor(g);
    if (!t) return false;
    auto multiply = [](LocalPauli a, const LocalPauli& b) {
        a.phase = static_cast<std::uint8_t>((a.phase + b.phase + 2 * std::popcount(static_cast<unsigned>(a.z & b.x))) & 3);
        a.x ^= b.x;
        a.z ^= b.z;
        return a;
    };
    for (PauliString& row : rows_) {
        LocalPauli acc;
        bool touched = false;
        for (std::size_t j = 0; j < wires.size(); ++j) {
            const std::uint32_t q = wires[j].index;
            if (row.x[q]) { acc = multiply(acc, t->imageX[j]); touched = true; }
            if (row.z[q]) { acc = multiply(acc, t->imageZ[j]); touched = true; }
        }
        if (!touched) continue;
        for (std::size_t j = 0; j < wires.size(); ++j) {
            row.x[wires[j].index] = (acc.x >> j) & 1u;
            row.z[wires[j].index] = (acc.z >> j) & 1u;
        }
        row.phase = static_cast<std::uint8_t>((row.phase + acc.phase) & 3);
    }
    return true;
}

namespace {
bool applyAll(CliffordTableau& t, const ir::Circuit& c) {
    for (ir::NodeId id : c.topologicalOrder()) {
        const ir::Node& n = c.node(id);
        if (const auto* g = std::get_if<ir::Gate>(&n)) {
            if (!t.apply(*g)) return false;
        } else if (const auto* box = std::get_if<ir::Box>(&n)) {
            if (!applyAll(t, *box->body)) return false;
        } else if (!std::holds_alternative<ir::Barrier>(n) && !std::holds_alternative<ir::Delay>(n)) {
            return false;
        }
    }
    return true;
}
} // namespace

std::optional<CliffordTableau> cliffordTableauOf(const ir::Circuit& c) {
    CliffordTableau t(c.qubitCount());
    if (!applyAll(t, c)) return std::nullopt;
    return t;
}

} // namespace qlab::compiler
