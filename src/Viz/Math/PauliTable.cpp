// Spec 21 §3.11 — Pauli expectation table (see PauliTable.hpp).
#include "Viz/Math/PauliTable.hpp"
#include "Viz/Math/Statistics.hpp"
#include <bit>
#include <cmath>

namespace qlab::viz::math {
namespace {

using num::Complex;

// P|j⟩ = c_j |j ⊕ x⟩, c_j = phase · i^{n_Y} · (−1)^{popcount(j & z)} (T11 (2.3), Y = iXZ).
Complex coefficient(const qsim::PauliString& p, std::uint64_t j) {
    static constexpr Complex kPowI[4] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    const Complex c = kPowI[std::popcount(p.xMask() & p.zMask()) & 3] * p.phase();
    return (std::popcount(j & p.zMask()) & 1) ? -c : c;
}

Status checkWidth(const qsim::PauliString& p, std::size_t dim) {
    if (dim == 0 || !std::has_single_bit(dim))
        return fail(ErrorCode::InvalidArgument, "Pauli expectation: state dimension is not 2^n");
    const auto n = static_cast<std::size_t>(std::countr_zero(dim));
    if (p.size() != n)
        return fail(ErrorCode::InvalidArgument, "Pauli string '" + p.label() + "' spans " +
                                                    std::to_string(p.size()) +
                                                    " qubits, the state has " + std::to_string(n));
    if (n > 62)
        return fail(ErrorCode::OutOfRange, "Pauli expectation: too many qubits");
    return {};
}

} // namespace

Result<double> pauliExpectation(std::span<const Complex> psi, const qsim::PauliString& p) {
    QXL_TRY(checkWidth(p, psi.size()));
    const std::uint64_t x = p.xMask();
    Complex sum{};
    for (std::uint64_t j = 0; j < psi.size(); ++j)
        sum += std::conj(psi[j ^ x]) * coefficient(p, j) * psi[j];
    return sum.real();
}

Result<double> pauliExpectation(const num::Matrix& rho, const qsim::PauliString& p) {
    if (rho.rows != rho.cols)
        return fail(ErrorCode::InvalidArgument, "Pauli expectation: density matrix is not square");
    QXL_TRY(checkWidth(p, rho.rows));
    const std::uint64_t x = p.xMask();
    Complex sum{};
    for (std::uint64_t j = 0; j < rho.rows; ++j)
        sum += rho(j, j ^ x) * coefficient(p, j); // (ρP)_jj
    return sum.real();
}

bool MeasurementMap::measures(std::uint32_t qubit, char letter) const {
    return qubit < basis.size() && qubit < bitOfQubit.size() && bitOfQubit[qubit] >= 0 &&
           basis[qubit] == letter;
}

std::optional<PauliEstimate> pauliFromCounts(const data::Histogram& counts,
                                             const qsim::PauliString& p,
                                             const MeasurementMap& map) {
    if (counts.total() == 0 || p.size() > 64)
        return std::nullopt;
    std::uint64_t bitMask = 0; // classical bits whose parity gives the eigenvalue
    for (std::size_t q = 0; q < p.size(); ++q) {
        const char letter = p.op(q);
        if (letter == 'I')
            continue;
        if (!map.measures(static_cast<std::uint32_t>(q), letter))
            return std::nullopt;
        const auto bit = static_cast<std::uint32_t>(map.bitOfQubit[q]);
        if (bit >= 64 || bit >= counts.nbits())
            return std::nullopt;
        bitMask |= std::uint64_t{1} << bit;
    }
    double acc = 0.0;
    for (const auto& [label, c] : counts.raw()) {
        const std::uint64_t b = data::Histogram::indexFromLabel(label);
        acc += (std::popcount(b & bitMask) & 1) ? -static_cast<double>(c) : static_cast<double>(c);
    }
    PauliEstimate e;
    e.shots = counts.total();
    e.value = p.phase().real() * acc / static_cast<double>(e.shots);
    e.standardError = pauliStandardError(e.value, e.shots);
    return e;
}

std::vector<qsim::PauliString> singleQubitPaulis(std::uint32_t n) {
    std::vector<qsim::PauliString> out;
    out.reserve(std::size_t{3} * n);
    for (std::uint32_t q = 0; q < n; ++q)
        for (char letter : {'X', 'Y', 'Z'}) {
            const std::pair<QubitIndex, char> term{QubitIndex{q}, letter};
            out.push_back(qsim::PauliString::fromQubits(
                n, std::span<const std::pair<QubitIndex, char>>(&term, 1)));
        }
    return out;
}

std::string pauliRowLabel(const qsim::PauliString& p) {
    std::size_t support = 0, where = 0;
    for (std::size_t q = 0; q < p.size(); ++q)
        if (p.op(q) != 'I') {
            ++support;
            where = q;
        }
    if (support == 1)
        return std::string(1, p.op(where)) + std::to_string(where);
    return p.label();
}

Result<qsim::PauliString> parseUserPauli(std::string_view text, std::uint32_t n) {
    // Trim blanks; keep an optional sign.
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    auto parsed = qsim::PauliString::parse(text);
    if (!parsed)
        return fail(ErrorCode::InvalidArgument, "Pauli string: " + parsed.error().message);
    if (parsed->phase().imag() != 0.0)
        return fail(ErrorCode::InvalidArgument,
                    "Pauli string: an imaginary prefix is not Hermitian");
    if (parsed->size() > n)
        return fail(ErrorCode::OutOfRange, "Pauli string spans " + std::to_string(parsed->size()) +
                                               " qubits, the state has " + std::to_string(n));
    if (parsed->size() == n)
        return *parsed;
    std::vector<std::pair<QubitIndex, char>> terms;
    for (std::size_t q = 0; q < parsed->size(); ++q)
        terms.emplace_back(QubitIndex{static_cast<std::uint32_t>(q)}, parsed->op(q));
    return qsim::PauliString::fromQubits(n, terms, parsed->phase());
}

} // namespace qlab::viz::math
