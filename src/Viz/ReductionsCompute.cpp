// Spec 21 §2.1–2.3, §3.8–3.11, §3.17 — the worker-thread reductions of a snapshot.
#include "Numerics/Matrix.hpp"
#include "Viz/Math/PauliTable.hpp"
#include "Viz/Reductions.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz {
namespace {

using math::Complex;
using num::Matrix;

// The state a snapshot carries, in the order of preference: amplitudes, then density matrix.
struct Source {
    const qsim::Snapshot* snap = nullptr;
    std::span<const Complex> psi;
    const Matrix* rho = nullptr;
    std::uint32_t n = 0, levels = 2;
    bool pure() const { return !psi.empty(); }
    bool mixed() const { return rho != nullptr; }

    // Reduced state over `keep` at the site level (levels^k square), keep[0] least significant.
    Result<Matrix> reduced(std::span<const QubitIndex> keep) const {
        if (pure())
            return math::reducedSubset(psi, n, keep);
        if (mixed())
            return math::reducedFromDensity(*rho, n, levels, keep);
        for (const auto& r : snap->reduced) // what the backend attached (tableau-only snapshots)
            if (r.qubits.size() == keep.size() &&
                std::equal(r.qubits.begin(), r.qubits.end(), keep.begin()))
                return r.rho;
        return fail(ErrorCode::NotFound, "the snapshot carries no state to reduce");
    }
};

Matrix renormalised(Matrix m) {
    const double tr = num::trace(m).real();
    if (tr > 1e-300)
        m *= 1.0 / tr;
    return m;
}

Result<SingleReduction> reduceSingle(const Source& src, QubitIndex q) {
    const QubitIndex keep[1] = {q};
    QXL_TRY_ASSIGN(Matrix rho, src.reduced(keep));
    const auto siteLevels = static_cast<std::uint32_t>(rho.rows);
    SingleReduction out;
    out.qubit = q;
    QXL_TRY_ASSIGN(const Matrix block, math::computationalBlock(rho, 1, siteLevels));
    QXL_TRY_ASSIGN(out.bloch, math::blochVector(block));
    out.leakage = std::max(0.0, num::trace(rho).real() - num::trace(block).real());
    out.purity = math::purity(rho);
    out.entropyBits = math::entropyBits(rho);
    out.rho = std::move(rho);
    return out;
}

Result<PairReduction> reducePair(const Source& src, QubitIndex i, QubitIndex j) {
    const QubitIndex keep[2] = {i, j};
    QXL_TRY_ASSIGN(const Matrix rho, src.reduced(keep));
    PairReduction out;
    out.i = i;
    out.j = j;
    if (rho.rows == 4) {
        QXL_TRY_ASSIGN(out.measures, math::pairMeasures(rho));
        return out;
    }
    // Multi-level sites: entropies from the full site states, concurrence from the renormalised
    // computational block (it is defined for two qubits only).
    const auto siteLevels =
        static_cast<std::uint32_t>(std::lround(std::sqrt(static_cast<double>(rho.rows))));
    const QubitIndex first[1] = {QubitIndex{0}}, second[1] = {QubitIndex{1}};
    QXL_TRY_ASSIGN(const Matrix rhoI, math::reducedFromDensity(rho, 2, siteLevels, first));
    QXL_TRY_ASSIGN(const Matrix rhoJ, math::reducedFromDensity(rho, 2, siteLevels, second));
    QXL_TRY_ASSIGN(const Matrix block, math::computationalBlock(rho, 2, siteLevels));
    QXL_TRY_ASSIGN(const math::PairMeasures q, math::pairMeasures(renormalised(block)));
    out.measures.entropyI = math::entropyBits(rhoI);
    out.measures.entropyJ = math::entropyBits(rhoJ);
    out.measures.entropyIJ = math::entropyBits(rho);
    out.measures.mutualInformation =
        std::max(0.0, out.measures.entropyI + out.measures.entropyJ - out.measures.entropyIJ);
    out.measures.concurrence = q.concurrence;
    return out;
}

Status checkQubits(std::span<const QubitIndex> qs, std::uint32_t n, const char* what) {
    for (QubitIndex q : qs)
        if (q.get() >= n)
            return fail(ErrorCode::OutOfRange, std::string(what) + ": qubit " +
                                                   std::to_string(q.get()) + " is outside the " +
                                                   std::to_string(n) + "-qubit register");
    return {};
}

} // namespace

Result<Reductions> computeReductions(const qsim::Snapshot& snap, const ReductionRequest& req,
                                     std::stop_token stop) {
    Reductions out;
    out.gateIndex = snap.gateIndex;
    out.simTimePs = snap.simTimePs;
    out.nQubits = snap.nQubits;
    out.levels = snap.levels;
    out.cls = snap.cls;
    const auto cancelled = [&]() { return stop.stop_requested(); };

    Source src;
    src.snap = &snap;
    src.n = snap.nQubits;
    src.levels = std::max<std::uint32_t>(2, snap.levels);
    if (snap.amplitudes && !snap.amplitudes->empty())
        src.psi = *snap.amplitudes;
    else if (snap.densityMatrix && !snap.densityMatrix->empty())
        src.rho = &*snap.densityMatrix;
    if (src.pure() && src.psi.size() != (std::size_t{1} << src.n))
        return fail(ErrorCode::InvalidArgument, "snapshot: amplitude count does not match nQubits");

    QXL_TRY(checkQubits(req.qubits, src.n, "reduction request"));
    std::vector<QubitIndex> qubits = req.qubits;
    if (qubits.empty())
        for (std::uint32_t q = 0; q < src.n; ++q)
            qubits.emplace_back(q);
    std::sort(qubits.begin(), qubits.end());
    qubits.erase(std::unique(qubits.begin(), qubits.end()), qubits.end());

    if (req.singles)
        for (QubitIndex q : qubits) {
            if (cancelled())
                return fail(ErrorCode::Cancelled, "reductions cancelled");
            auto s = reduceSingle(src, q);
            if (s)
                out.singles.push_back(std::move(*s));
            else
                out.notes.push_back("rho of q" + std::to_string(q.get()) + ": " +
                                    s.error().message);
        }

    if (req.pairs) {
        if (qubits.size() > kPairReductionMaxQubits) {
            out.notes.push_back("pair reductions need a qubit subset of at most 20 (spec 21 §3.8)");
        } else {
            for (std::size_t a = 0; a < qubits.size(); ++a)
                for (std::size_t b = a + 1; b < qubits.size(); ++b) {
                    if (cancelled())
                        return fail(ErrorCode::Cancelled, "reductions cancelled");
                    auto p = reducePair(src, qubits[a], qubits[b]);
                    if (p)
                        out.pairs.push_back(*p);
                    else
                        out.notes.push_back("pair (" + std::to_string(qubits[a].get()) + "," +
                                            std::to_string(qubits[b].get()) +
                                            "): " + p.error().message);
                }
        }
    }

    for (const auto& subset : req.subsets) {
        if (cancelled())
            return fail(ErrorCode::Cancelled, "reductions cancelled");
        QXL_TRY(checkQubits(subset, src.n, "subset request"));
        if (subset.empty() || subset.size() > kSubsetMaxQubits) {
            out.notes.push_back("a reduced density matrix holds 1 to 8 qubits (spec 21 §3.5)");
            continue;
        }
        auto rho = src.reduced(subset);
        if (rho)
            rho = math::computationalBlock(*rho, static_cast<std::uint32_t>(subset.size()),
                                           src.pure() ? 2u : src.levels);
        if (rho)
            out.subsets.push_back({subset, std::move(*rho)});
        else
            out.notes.push_back("reduced state: " + rho.error().message);
    }

    if (req.schmidtPartition) {
        QXL_TRY(checkQubits(*req.schmidtPartition, src.n, "Schmidt partition"));
        if (!src.pure())
            out.notes.push_back("the Schmidt spectrum needs a pure state (state-vector snapshot)");
        else if (auto s = math::schmidtSpectrum(src.psi, src.n, *req.schmidtPartition))
            out.schmidt = std::move(*s);
        else
            out.notes.push_back("Schmidt spectrum: " + s.error().message);
    }

    if (req.singleQubitPaulis || !req.pauliStrings.empty()) {
        std::optional<Matrix>
            qubitRho; // computational block of a multi-level density matrix, built once
        if (!src.pure() && src.mixed()) {
            auto block = math::computationalBlock(*src.rho, src.n, src.levels);
            if (block)
                qubitRho = std::move(*block);
        }
        const auto expect = [&](const qsim::PauliString& p) -> Result<double> {
            if (src.pure())
                return math::pauliExpectation(src.psi, p);
            if (qubitRho)
                return math::pauliExpectation(*qubitRho, p);
            return fail(ErrorCode::NotFound, "the snapshot carries no state");
        };
        const auto add = [&](const qsim::PauliString& p, bool user) {
            auto v = expect(p);
            if (v)
                out.paulis.push_back({math::pauliRowLabel(p), p.label(), *v, user});
            else
                out.notes.push_back("<" + p.label() + ">: " + v.error().message);
        };
        if (req.singleQubitPaulis)
            for (const auto& p : math::singleQubitPaulis(src.n)) {
                if (cancelled())
                    return fail(ErrorCode::Cancelled, "reductions cancelled");
                add(p, false);
            }
        for (const auto& text : req.pauliStrings) {
            if (cancelled())
                return fail(ErrorCode::Cancelled, "reductions cancelled");
            auto p = math::parseUserPauli(text, src.n);
            if (p)
                add(*p, true);
            else
                out.notes.push_back(p.error().message);
        }
    }

    if (req.topAmplitudes > 0) {
        if (src.pure()) {
            math::AmplitudeFilter f;
            f.maxEntries = req.topAmplitudes;
            f.order = math::AmplitudeOrder::ByMagnitude;
            out.topAmplitudes = math::selectAmplitudes(src.psi, f);
        } else {
            out.notes.push_back("amplitudes need a state-vector snapshot");
        }
    }

    if (req.wigner) {
        if (!req.modeState)
            out.notes.push_back("Wigner function: no oscillator mode state was supplied");
        else if (auto g = math::wignerGrid(*req.modeState, *req.wigner))
            out.wigner = std::move(*g);
        else
            out.notes.push_back("Wigner function: " + g.error().message);
    }
    return out;
}

} // namespace qlab::viz
