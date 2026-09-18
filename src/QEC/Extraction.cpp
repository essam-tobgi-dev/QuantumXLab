// Spec 16 §2.1, §3; T09 §5.2–§5.3 — planning a memory experiment: logical preparation, R rounds of
// syndrome extraction, transversal data readout, and the detectors / observables of the record.
#include "QEC/Encoder.hpp"
#include "QEC/Extraction.hpp"
#include <format>

namespace qlab::qec {
namespace {

class Planner {
public:
    Planner(const StabilizerCode& code, const ExtractionOptions& options) : code_(code), opt_(options) {
        ex_.codeId = code.id;
        ex_.options = options;
        ex_.nData = code.n;
        ex_.nAncilla = code.checkCount();
    }

    Result<MemoryExperiment> plan() {
        QXL_TRY(validate());
        QXL_TRY(chooseDataBasis());
        ex_.schedule.qubits = ex_.nData + ex_.nAncilla;
        ex_.schedule.bits = ex_.nAncilla * opt_.rounds + (opt_.finalDataMeasurement ? ex_.nData : 0u);
        if (opt_.includeLogicalPrep) {
            if (code_.isCss()) prepareData();
            else QXL_TRY(encodeData());
        }
        for (std::uint32_t r = 0; r < opt_.rounds; ++r) {
            push({OpKind::RoundStart, r});
            if (code_.family == CodeFamily::SurfaceRotated) QXL_TRY(layeredRound(r));
            else serialRound(r);
        }
        if (opt_.finalDataMeasurement) readOutData();
        defineDetectors();
        ex_.qubitCoords = code_.dataLayout;
        for (const AncillaSpec& a : code_.ancillas) ex_.qubitCoords.push_back(a.coord);
        return std::move(ex_);
    }

private:
    void push(Op op) { ex_.schedule.ops.push_back(op); }
    std::uint32_t anc(std::uint32_t j) const { return ex_.nData + j; }

    Status validate() const {
        if (opt_.rounds == 0) return fail(err::BadOptions, "a memory experiment needs at least one extraction round");
        if (code_.checkCount() == 0) return fail(err::BadOptions, std::format("code '{}' has no generator to measure", code_.id));
        if (code_.ancillas.size() != code_.stabilizers.size() || code_.dataLayout.size() != code_.n)
            return fail(err::BadLayout, std::format("code '{}': layout does not give one ancilla per generator", code_.id));
        for (std::uint32_t j = 0; j < code_.checkCount(); ++j)
            if (code_.stabilizers[j].phase != 0)
                return fail(err::BadOptions, std::format("code '{}': generator {} carries a sign; the projection "
                                                         "preparation assumes +1 generators", code_.id, j));
        if (code_.k == 0 || code_.logicalX.size() != code_.k || code_.logicalZ.size() != code_.k)
            return fail(err::BadLogical, std::format("code '{}' has no logical qubit to store", code_.id));
        return {};
    }

    // Each data qubit is prepared in (and read out along) the letter the memory-basis logical
    // operators have on it; qubits outside their support follow the operators' common type, so that
    // the checks of that type are deterministic from round 0 and closed by the readout.
    Status chooseDataBasis() {
        ex_.dataBasis.assign(code_.n, 'I');
        bool allX = true, allZ = true;
        for (std::uint32_t j = 0; j < code_.k; ++j) {
            const PauliString& l = code_.logical(opt_.basis, j);
            allX = allX && l.isXType();
            allZ = allZ && l.isZType();
            for (std::uint32_t q : l.support()) {
                if (ex_.dataBasis[q] != 'I' && ex_.dataBasis[q] != l.letter(q))
                    return fail(err::BadOptions, std::format("code '{}': logical operators of the {} basis disagree on "
                                                             "qubit {}; no transversal readout", code_.id, basisName(opt_.basis), q));
                ex_.dataBasis[q] = l.letter(q);
            }
        }
        const char fill = (allX && !allZ) ? 'X' : 'Z';
        for (char& b : ex_.dataBasis)
            if (b == 'I') b = fill;
        return {};
    }

    void prepareData() {
        for (std::uint32_t q = 0; q < ex_.nData; ++q) {
            push({OpKind::Reset, q});
            if (ex_.dataBasis[q] != 'Z') push({OpKind::H, q});
            if (ex_.dataBasis[q] == 'Y') push({OpKind::S, q});   // S H |0⟩ = |+i⟩
        }
    }

    // No product state is an eigenstate of mixed checks, and an error before a projecting round
    // would then be invisible: non-CSS codes start from their explicit encoder instead (spec 16 §3),
    // which makes every check deterministic from round 0.
    Status encodeData() {
        QXL_TRY_ASSIGN(const Schedule encoder, planEncoder(code_));
        for (std::uint32_t q = 0; q < ex_.nData; ++q) push({OpKind::Reset, q});
        if (opt_.basis == LogicalBasis::X)
            for (std::uint32_t l = 0; l < code_.k; ++l) push({OpKind::H, l});   // logical input |+⟩
        for (const Op& op : encoder.ops) push(op);
        encoded_ = true;
        return {};
    }

    void readOutData() {
        push({OpKind::Tick});
        for (std::uint32_t q = 0; q < ex_.nData; ++q) {
            if (ex_.dataBasis[q] == 'Y') push({OpKind::Sdg, q});
            if (ex_.dataBasis[q] != 'Z') push({OpKind::H, q});
            push({OpKind::Measure, q, 0, ex_.dataBit(q)});
        }
    }

    // Z-type generator: data → ancilla CNOTs. Anything else: ancilla in |+⟩ controls the letter.
    Op coupling(std::uint32_t j, std::uint32_t q) const {
        const char letter = code_.stabilizers[j].letter(q);
        if (code_.checkType(j) == CheckType::Z) return {OpKind::CX, q, anc(j)};
        return {letter == 'X' ? OpKind::CX : (letter == 'Z' ? OpKind::CZ : OpKind::CY), anc(j), q};
    }

    void serialRound(std::uint32_t r) {
        for (std::uint32_t j = 0; j < ex_.nAncilla; ++j) {
            const bool hadamard = code_.checkType(j) != CheckType::Z;
            push({OpKind::Reset, anc(j)});
            if (hadamard) push({OpKind::H, anc(j)});
            for (std::uint32_t q : code_.ancillas[j].order) push(coupling(j, q));
            if (hadamard) push({OpKind::H, anc(j)});
            push({OpKind::Measure, anc(j), 0, ex_.syndromeBit(r, j)});
        }
    }

    // Spec 16 §2.1: reset → h on X ancillas → four CNOT layers → h → measure, all ancillas at once.
    Status layeredRound(std::uint32_t r) {
        for (std::uint32_t j = 0; j < ex_.nAncilla; ++j) push({OpKind::Reset, anc(j)});
        push({OpKind::Tick});
        hadamardLayer();
        for (int layer = 0; layer < 4; ++layer) {
            for (std::uint32_t j = 0; j < ex_.nAncilla; ++j)
                for (std::uint32_t q : code_.ancillas[j].order) {
                    const int l = tomitaSvoreLayer(code_.checkType(j), code_.ancillas[j].coord, code_.dataLayout[q]);
                    if (l < 0)
                        return fail(err::BadLayout, std::format("code '{}': data qubit {} is not a diagonal neighbour of "
                                                                "ancilla {}", code_.id, q, j));
                    if (l == layer) push(coupling(j, q));
                }
            push({OpKind::Tick});
        }
        hadamardLayer();
        for (std::uint32_t j = 0; j < ex_.nAncilla; ++j) push({OpKind::Measure, anc(j), 0, ex_.syndromeBit(r, j)});
        return {};
    }

    void hadamardLayer() {
        for (std::uint32_t j = 0; j < ex_.nAncilla; ++j)
            if (code_.checkType(j) != CheckType::Z) push({OpKind::H, anc(j)});
        push({OpKind::Tick});
    }

    // True when the product state of `dataBasis` is a +1 eigenstate of generator j, equivalently
    // when the transversal readout measures it.
    bool fixedByDataBasis(std::uint32_t j) const {
        for (std::uint32_t q : code_.stabilizers[j].support())
            if (code_.stabilizers[j].letter(q) != ex_.dataBasis[q]) return false;
        return true;
    }

    void defineDetectors() {
        const std::uint32_t m = ex_.nAncilla, layers = ex_.layers();
        ex_.detectorIndex.assign(std::size_t(layers) * m, kNoIndex);
        auto add = [&](std::uint32_t j, std::uint32_t layer, std::vector<std::uint32_t> bits) {
            ex_.detectorIndex[std::size_t(layer) * m + j] = static_cast<std::uint32_t>(ex_.detectors.size());
            ex_.detectors.push_back({j, layer, code_.checkType(j), std::move(bits)});
        };
        for (std::uint32_t layer = 0; layer < layers; ++layer)
            for (std::uint32_t j = 0; j < m; ++j) {
                if (layer == 0) {
                    const bool fixed = opt_.includeLogicalPrep ? (encoded_ || fixedByDataBasis(j)) : opt_.assumeCodeStateInput;
                    if (fixed) add(j, 0, {ex_.syndromeBit(0, j)});
                } else if (layer < opt_.rounds) {
                    add(j, layer, {ex_.syndromeBit(layer - 1, j), ex_.syndromeBit(layer, j)});   // T09 (5.2)
                } else if (fixedByDataBasis(j)) {
                    std::vector<std::uint32_t> bits = {ex_.syndromeBit(opt_.rounds - 1, j)};
                    for (std::uint32_t q : code_.stabilizers[j].support()) bits.push_back(ex_.dataBit(q));
                    add(j, layer, std::move(bits));
                }
            }
        if (!opt_.finalDataMeasurement) return;
        for (std::uint32_t l = 0; l < code_.k; ++l) {
            std::vector<std::uint32_t> bits;
            for (std::uint32_t q : code_.logical(opt_.basis, l).support()) bits.push_back(ex_.dataBit(q));
            ex_.observables.push_back(std::move(bits));
        }
    }

    const StabilizerCode& code_;
    ExtractionOptions opt_;
    MemoryExperiment ex_;
    bool encoded_ = false;
};

std::vector<std::uint8_t> parities(std::span<const std::uint8_t> bits, const auto& groups, auto bitsOf) {
    std::vector<std::uint8_t> out;
    out.reserve(groups.size());
    for (const auto& g : groups) {
        std::uint8_t p = 0;
        for (std::uint32_t b : bitsOf(g))
            if (b < bits.size()) p ^= static_cast<std::uint8_t>(bits[b] & 1u);
        out.push_back(p);
    }
    return out;
}

} // namespace

std::string_view opName(OpKind k) {
    static constexpr std::string_view names[] = {"h",    "s",     "sdg",     "x",    "y",         "z", "cx", "cy", "cz",
                                                 "swap", "reset", "measure", "tick", "round_start"};
    return names[static_cast<std::size_t>(k)];
}

std::uint32_t MemoryExperiment::detectorAt(std::uint32_t check, std::uint32_t layer) const {
    const std::size_t i = std::size_t(layer) * nAncilla + check;
    return (check < nAncilla && i < detectorIndex.size()) ? detectorIndex[i] : kNoIndex;
}

std::vector<std::uint8_t> MemoryExperiment::detectionEvents(std::span<const std::uint8_t> bits) const {
    return parities(bits, detectors, [](const Detector& d) -> const std::vector<std::uint32_t>& { return d.bits; });
}

std::vector<std::uint8_t> MemoryExperiment::observableValues(std::span<const std::uint8_t> bits) const {
    return parities(bits, observables, [](const std::vector<std::uint32_t>& o) -> const std::vector<std::uint32_t>& { return o; });
}

Result<MemoryExperiment> planMemoryExperiment(const StabilizerCode& code, const ExtractionOptions& options) {
    return Planner(code, options).plan();
}

} // namespace qlab::qec
