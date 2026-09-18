#include "Instruments/StateAccess.hpp"
#include "Numerics/Numerics.hpp"
#include <algorithm>
#include <format>

namespace qlab::instr {

Result<num::Matrix> reducedDensity(const qsim::Snapshot& s, std::span<const std::uint32_t> qubits) {
    std::vector<std::size_t> keep(qubits.begin(), qubits.end());
    std::sort(keep.begin(), keep.end());
    keep.erase(std::unique(keep.begin(), keep.end()), keep.end());
    if (keep.empty()) return fail(err::BadInput, "state: no qubits selected");
    if (keep.back() >= s.nQubits)
        return fail(err::BadInput, std::format("state: qubit {} is outside the {}-qubit register", keep.back(), s.nQubits));
    for (auto const& r : s.reduced) { // already reduced by the runtime (large registers)
        std::vector<std::size_t> have;
        for (auto q : r.qubits) have.push_back(q.get());
        if (std::is_sorted(have.begin(), have.end()) && have == keep) return r.rho;
    }
    const std::size_t levels = s.levels;
    const std::size_t dim = num::ipow(levels, s.nQubits);
    if (s.densityMatrix) {
        if (s.densityMatrix->rows != dim || s.densityMatrix->cols != dim)
            return fail(err::BadInput, std::format("state: density matrix is {}x{}, expected {}", s.densityMatrix->rows, s.densityMatrix->cols, dim));
        if (keep.size() == s.nQubits) return *s.densityMatrix;
        const std::vector<std::size_t> dims(s.nQubits, levels);
        return num::partialTrace(*s.densityMatrix, dims, keep);
    }
    if (s.amplitudes) {
        if (s.amplitudes->size() != dim)
            return fail(err::BadInput, std::format("state: {} amplitudes, expected {}", s.amplitudes->size(), dim));
        return num::reducedState(*s.amplitudes, s.nQubits, keep, levels);
    }
    return fail(err::BadInput, "state: the snapshot holds neither amplitudes, a density matrix nor this reduced state");
}

Result<std::vector<double>> levelPopulations(const qsim::Snapshot& s, std::uint32_t qubit) {
    const std::uint32_t one[] = {qubit};
    QXL_TRY_ASSIGN(num::Matrix rho, reducedDensity(s, one));
    std::vector<double> p(rho.rows);
    for (std::size_t l = 0; l < rho.rows; ++l) p[l] = rho(l, l).real();
    return p;
}

num::Matrix qubitBlock(const num::Matrix& site) {
    num::Matrix b(2, 2);
    for (std::size_t i = 0; i < 2 && i < site.rows; ++i)
        for (std::size_t j = 0; j < 2 && j < site.cols; ++j) b(i, j) = site(i, j);
    return b;
}

Result<num::Matrix> fullDensity(const qsim::Snapshot& s) {
    if (s.densityMatrix) return *s.densityMatrix;
    if (!s.amplitudes) return fail(err::BadInput, "state: the snapshot holds neither amplitudes nor a density matrix");
    if (s.nQubits > 12) return fail(err::BadInput, std::format("state: a {}-qubit pure state is too large to expand", s.nQubits));
    return num::projector(*s.amplitudes);
}

} // namespace qlab::instr
