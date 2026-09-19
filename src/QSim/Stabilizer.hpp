#pragma once
// Spec 07 §4, T09/T11 — Aaronson–Gottesman stabilizer tableau backend.
#include "QSim/Backend.hpp"
#include <cstdint>

namespace qlab::qsim {

// A Pauli on k ≤ 3 qubits with phase i^p: used for Clifford recognition (image of generators).
struct LocalPauli {
    std::uint32_t x = 0, z = 0;
    std::uint32_t phase = 0; /* 0..3, i^phase */
};

class StabilizerBackend final : public IBackend {
  public:
    Capabilities capabilities() const override;
    Kind kind() const override { return Kind::Stabilizer; }
    std::uint32_t nQubits() const override { return n_; }

    Status allocate(std::uint32_t nQubits, std::uint32_t levelsPerSite = 2) override;
    Status reset(std::span<const QubitIndex> qubits, core::Random& rng) override;
    Status applyGate(const Matrix& u, std::span<const QubitIndex> targets) override;
    Status applyControlled(const Matrix& u, std::span<const QubitIndex> controls,
                           std::span<const QubitIndex> targets) override;
    Status apply(const GateOp& op) override;
    Status applyChannel(const Kraus& kraus, std::span<const QubitIndex> targets)
        override; // refused (Pauli frames are Runtime's)
    Result<Outcome> measure(std::span<const QubitIndex> qubits, core::Random& rng) override;
    Result<double> expectation(const PauliString& p) const override;
    Result<Probabilities> probabilities(std::span<const QubitIndex> qubits) const override;
    Result<Snapshot> snapshot(const SnapshotRequest& req) const override;
    Result<Counts> sample(std::span<const QubitIndex> qubits, std::uint64_t shots,
                          core::Random& rng) const override;
    std::size_t bytesAllocated() const override { return words_.size() * sizeof(std::uint64_t); }
    double stateNorm() const override { return 1.0; }
    std::unique_ptr<IBackend> clone() const override;

    // Named Clifford gates (direct word-parallel updates).
    void h(std::uint32_t q);
    void s(std::uint32_t q);
    void sdg(std::uint32_t q);
    void x(std::uint32_t q);
    void y(std::uint32_t q);
    void z(std::uint32_t q);
    void sx(std::uint32_t q);
    void cnot(std::uint32_t c, std::uint32_t t);
    void cz(std::uint32_t a, std::uint32_t b);
    void swap(std::uint32_t a, std::uint32_t b);
    // Apply a Pauli string (frame update, spec 07 §4 noise).
    Status applyPauli(const PauliString& p);
    TableauExport exportTableau() const;
    // Entanglement entropy (bits) of a subsystem from the tableau rank (spec 07 §8); NaN when the
    // backend is unallocated or the subsystem has out-of-range or repeated qubits.
    double entanglementEntropy(std::span<const QubitIndex> subsystem) const;

    // Recognise a unitary on k ≤ 3 qubits as Clifford: returns images of X_j and Z_j under U·U†.
    static Result<std::vector<LocalPauli>> cliffordImages(const Matrix& u, std::size_t k);

  private:
    bool getX(std::size_t row, std::uint32_t q) const {
        return (words_[row * W_ + (q >> 6)] >> (q & 63)) & 1;
    }
    bool getZ(std::size_t row, std::uint32_t q) const {
        return (words_[row * W_ + zOff_ + (q >> 6)] >> (q & 63)) & 1;
    }
    bool getR(std::size_t row) const { return r_[row]; }
    void setX(std::size_t row, std::uint32_t q, bool v);
    void setZ(std::size_t row, std::uint32_t q, bool v);
    void rowsum(std::size_t h, std::size_t i); // row h ← row h · row i
    int rowsumPhase(std::size_t h, std::size_t i) const;
    void rowsumInto(std::vector<std::uint64_t>& acc, int& phase, std::size_t i) const;
    void applyImages(std::span<const std::uint32_t> targets, std::span<const LocalPauli> images);
    Result<bool> measureOne(std::uint32_t q, core::Random& rng, double* prob);
    std::uint32_t n_ = 0;
    std::size_t W_ = 0, zOff_ = 0;     // words per row: 2*ceil(n/64); zOff_ = ceil(n/64)
    std::vector<std::uint64_t> words_; // 2n rows × W_ words
    std::vector<std::uint8_t> r_;      // 2n phase bits
    bool allocated_ = false;
};

} // namespace qlab::qsim
