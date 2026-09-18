#pragma once
// Spec 23 §9 — Simulator-only state export. The state vector and the density matrix are written as
// NumPy `.npy` v1.0 files (dtype `<c16`, C order) so that `numpy.load` reads them directly; the
// stabilizer tableau is text, one signed Pauli string per line. Each export has a JSON sidecar
// (envelope kind `state_export`) with the qubit order, the device mapping, the snapshot's gate
// index, the fidelity class and the Simulator-only notice.
#include "Numerics/Types.hpp"
#include "QSim/Types.hpp"
#include "Report/Types.hpp"
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace qlab::report {

// What the sidecar records about the state it accompanies.
struct StateContext {
    RunIdentity identity;
    std::vector<std::uint32_t> qubits;   // simulator index → device qubit (spec 15 §4)
    std::uint32_t levels = 2;            // 2, or 3 for a pulse-level / leakage state
    std::string levelOrder;              // "" = "level index = Σ l_k d^k, little-endian"
    std::string note;
};

// The exact header bytes of a `.npy` v1.0 file: `\x93NUMPY`, version 1.0, the little-endian uint16
// header length, then the padded dict, so that the total is a multiple of 64 and ends with '\n'.
std::string npyHeader(std::string_view descr, std::span<const std::size_t> shape, bool fortranOrder = false);
// Writes `npyHeader` followed by `data` (little-endian hosts only, as `descr` claims).
Status writeNpy(const std::filesystem::path& path, std::string_view descr, std::span<const std::size_t> shape,
                std::span<const std::byte> data);
Status writeNpyComplex(const std::filesystem::path& path, std::span<const num::Complex> values,
                       std::span<const std::size_t> shape);

// Sidecar document (`data` of the envelope). `shape` and `dtype` describe the companion file.
core::Json stateSidecar(const qsim::Snapshot& s, const StateContext& ctx, std::string_view file,
                        std::string_view dtype, std::span<const std::size_t> shape);

// `<base>.npy` + `<base>.json`. Fails when the snapshot carries no amplitudes / density matrix /
// tableau — a state the backend cannot produce is never faked (spec 00 §5).
Status exportStateVector(const std::filesystem::path& base, const qsim::Snapshot& s, const StateContext& ctx);
Status exportDensityMatrix(const std::filesystem::path& base, const qsim::Snapshot& s, const StateContext& ctx);
// `<base>.txt` + `<base>.json`: one stabilizer generator per line, signed Pauli strings.
Status exportTableau(const std::filesystem::path& base, const qsim::Snapshot& s, const StateContext& ctx);

// The tableau text of §9 (also used by the report).
std::string tableauText(const qsim::TableauExport& t);

} // namespace qlab::report
