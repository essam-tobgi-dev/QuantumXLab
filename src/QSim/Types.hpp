#pragma once
// Spec 07 §1 — common types of the simulation engine.
#include "Core/Error.hpp"
#include "Core/Fidelity.hpp"
#include "Core/StrongType.hpp"
#include "Numerics/Matrix.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace qlab::qsim {

using num::Complex;
using num::Matrix;
using qlab::FidelityClass; // spec 00 §5 (Core vocabulary)

enum class Kind { StateVector, DensityMatrix, Stabilizer, Lindblad, Trajectories };
std::string_view kindName(Kind k);

struct Capabilities {
    Kind kind{};
    std::uint32_t maxQubits = 0;
    bool exactNoise = false;
    bool stochasticNoise = false;
    bool nonClifford = false;
    bool midCircuitMeasure = false;
    bool multiLevel = false;
    bool timeDomain = false;
    bool fullStateReadback = false;
};

// Kraus operator set Σ K†K = I (spec 08 defines channels; the engine only applies them).
using Kraus = std::vector<Matrix>;

// Gate classification used for kernel fast paths (spec 07 §2.2–2.3). Assigned by the IR, never by
// inspecting matrix entries at run time.
enum class GateClass : std::uint8_t {
    Generic,
    Identity,
    Diagonal,
    PauliX,
    PauliZ,
    Cnot,
    Cz,
    Swap,
    Diagonal2
};

// A gate ready for a backend: matrix (2^k × 2^k), little-endian targets, optional controls.
struct GateOp {
    Matrix matrix;
    std::vector<QubitIndex> targets;
    std::vector<QubitIndex> controls;
    GateClass cls = GateClass::Generic;
    std::string name; // for diagnostics only
    std::uint64_t opIndex = 0;
};

// Pauli string. Label is written most-significant qubit first: "ZI" = Z on q1, I on q0 (T01
// conventions). Any width is allowed (the stabilizer register holds up to 10^4 qubits); op(q) is
// authoritative.
class PauliString {
  public:
    PauliString() = default;
    // Parse "IXYZ" (with optional leading sign/phase "-", "+", "i", "-i").
    static Result<PauliString> parse(std::string_view label);
    // From explicit per-qubit letters, index = qubit. Terms on qubits ≥ n are ignored.
    static PauliString fromQubits(std::size_t n, std::span<const std::pair<QubitIndex, char>> terms,
                                  Complex phase = 1.0);
    std::size_t size() const { return ops_.size(); }
    char op(std::size_t qubit) const { return ops_[qubit]; } // 'I','X','Y','Z'
    Complex phase() const { return phase_; }
    std::string label() const; // MSB-first, no phase
    // X-or-Y and Z-or-Y bit masks of qubits 0..63 only: exact for every amplitude-based backend
    // (n ≤ 34); wider strings must be read through op(q).
    std::uint64_t xMask() const { return x_; }
    std::uint64_t zMask() const { return z_; }
    bool isIdentity() const;

  private:
    void setLetter(std::size_t qubit, char c);
    std::vector<char> ops_;
    std::uint64_t x_ = 0, z_ = 0;
    Complex phase_ = 1.0;
};

struct Outcome {
    std::vector<std::uint8_t> bits; // bits[i] = outcome of qubits[i] in the order requested
    double probability = 1.0;       // joint probability of this branch
};

using Probabilities = std::vector<double>; // index = little-endian over the requested qubits
using Counts = std::unordered_map<std::string,
                                  std::uint64_t>; // key: bitstring MSB-first over requested qubits

struct ReducedState {
    std::vector<QubitIndex> qubits;
    Matrix rho;
};

// Stabilizer tableau export (spec 07 §1.1).
struct TableauExport {
    std::uint32_t n = 0;
    std::vector<std::string> stabilizers; // n generators, "+ZZI" style
    std::vector<std::string> destabilizers;
};

struct SnapshotRequest {
    bool amplitudes = false;
    bool probabilities = false;
    bool reducedStates = false;
    std::vector<std::vector<QubitIndex>> subsystems;
    bool tableau = false;
};

struct Snapshot {
    Kind kind{};
    std::uint32_t nQubits = 0;
    std::uint32_t levels = 2;
    std::uint64_t gateIndex = 0;
    double simTimePs = 0.0;
    std::optional<std::vector<Complex>> amplitudes; // Simulator-only
    std::optional<std::vector<double>> probabilities;
    std::optional<Matrix> densityMatrix;  // Simulator-only (DM/Lindblad)
    std::vector<ReducedState> reduced;    // Simulator-only
    std::optional<TableauExport> tableau; // Simulator-only
    FidelityClass cls = FidelityClass::Exact;
};

// Error codes owned by this module (ErrorCode::QSim_ block).
namespace err {
inline constexpr ErrorCode NotAllocated = ErrorCode::QSim_ + 1;
inline constexpr ErrorCode TooLarge = ErrorCode::QSim_ + 2;
inline constexpr ErrorCode BadTargets = ErrorCode::QSim_ + 3;
inline constexpr ErrorCode NotUnitary = ErrorCode::QSim_ + 4;
inline constexpr ErrorCode NotClifford = ErrorCode::QSim_ + 5;
inline constexpr ErrorCode Unsupported = ErrorCode::QSim_ + 6;
inline constexpr ErrorCode BadKraus = ErrorCode::QSim_ + 7;
inline constexpr ErrorCode BadPauli = ErrorCode::QSim_ + 8;
} // namespace err

// Helpers shared by backends.
std::string bitsToKey(std::span<const std::uint8_t> bits); // MSB-first string
Counts countsFromIndices(std::span<const std::size_t> idx, std::size_t nBits);
// Histogram form: hist[i] = number of shots that produced basis index i.
Counts countsFromHistogram(std::span<const std::size_t> hist, std::size_t nBits);
bool validTargets(std::span<const QubitIndex> t, std::uint32_t n);

} // namespace qlab::qsim
