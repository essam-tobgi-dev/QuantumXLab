// Spec 14 §10 — equivalence checking: mapping, wire compaction, and the three methods.
#include "Compiler/Equivalence.hpp"
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/EquivalenceImpl.hpp"
#include "Compiler/Tableau.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::compiler {
namespace {
using detail::SimProgram;
using detail::StateVec;
constexpr std::uint32_t kUnmapped = 0xFFFFFFFFu;

struct Mapping {
    std::vector<std::uint32_t> start, end;      // program qubit → compiled wire before / after
    std::vector<std::uint32_t> active;          // program qubits that take part, ascending
    std::vector<std::uint32_t> refMap, compMap; // circuit wire → state wire
    std::uint32_t compWires = 0;
};

Result<Mapping> mappingOf(const ir::Circuit& reference, const ir::Circuit& compiled) {
    Mapping m;
    const std::uint32_t k = reference.qubitCount();
    m.start = metaIndices(compiled, "layout").value_or(Layout::identity(k).v2p);
    if (m.start.empty())
        m.start = Layout::identity(k).v2p;
    m.end = metaIndices(compiled, "final_layout").value_or(m.start);
    if (m.start.size() != k || m.end.size() != k)
        return fail(
            ErrorCode::InvalidArgument,
            std::format("the compiled circuit maps {} program qubit(s) but the reference has {}",
                        m.start.size(), k));
    for (std::uint32_t v = 0; v < k; ++v)
        if (m.start[v] >= compiled.qubitCount() || m.end[v] >= compiled.qubitCount())
            return fail(
                ErrorCode::InvalidArgument,
                std::format("the layout of program qubit {} lies outside the compiled circuit", v));

    std::vector<std::uint8_t> refUsed(k, 0), compUsed(compiled.qubitCount(), 0);
    for (std::uint32_t w : usedWires(reference))
        if (w < k)
            refUsed[w] = 1;
    for (std::uint32_t w : usedWires(compiled))
        if (w < compUsed.size())
            compUsed[w] = 1;
    std::vector<std::uint8_t> keep = compUsed;
    m.refMap.assign(k, kUnmapped);
    for (std::uint32_t v = 0; v < k; ++v) {
        if (!refUsed[v] && !compUsed[m.start[v]] && !compUsed[m.end[v]] && m.start[v] == m.end[v])
            continue; // idle in both
        m.refMap[v] = static_cast<std::uint32_t>(m.active.size());
        m.active.push_back(v);
        keep[m.start[v]] = keep[m.end[v]] = 1;
    }
    m.compMap.assign(compiled.qubitCount(), kUnmapped);
    for (std::uint32_t p = 0; p < keep.size(); ++p)
        if (keep[p])
            m.compMap[p] = m.compWires++;
    return m;
}

// State index of the compiled register that holds reference basis index `y` on the given positions.
std::size_t scatter(std::size_t y, const Mapping& m, const std::vector<std::uint32_t>& position) {
    std::size_t index = 0;
    for (std::size_t j = 0; j < m.active.size(); ++j)
        if ((y >> j) & 1u)
            index |= std::size_t{1} << m.compMap[position[m.active[j]]];
    return index;
}

EquivalenceReport cliffordCompare(const CliffordTableau& ref, const CliffordTableau& comp,
                                  const Mapping& m) {
    EquivalenceReport r;
    r.method = EquivalenceMethod::Clifford;
    r.wires = comp.qubits();
    const std::uint32_t k = ref.qubits();
    r.inputs = 2 * k;
    for (std::uint32_t v = 0; v < k; ++v)
        for (int which = 0; which < 2; ++which) {
            const PauliString& want = which == 0 ? ref.imageX(v) : ref.imageZ(v);
            const PauliString& got = which == 0 ? comp.imageX(m.start[v]) : comp.imageZ(m.start[v]);
            PauliString expected;
            expected.x.assign(comp.qubits(), 0);
            expected.z.assign(comp.qubits(), 0);
            expected.phase = want.phase;
            for (std::uint32_t u = 0; u < k; ++u) {
                expected.x[m.end[u]] = want.x[u];
                expected.z[m.end[u]] = want.z[u];
            }
            if (expected == got)
                continue;
            r.worstOverlap = 0.0;
            r.detail = std::format(
                "U {}_{} U† is {} in the source but {} in the compiled circuit (physical wires)",
                which == 0 ? 'X' : 'Z', v, want.text(), got.text());
            return r;
        }
    r.equivalent = true;
    return r;
}

struct Simulation {
    const Mapping& m;
    const SimProgram& ref;
    const SimProgram& comp;
    std::uint32_t clbits;

    // Runs both circuits on the same input (amplitudes over the active program qubits) and returns
    // ⟨ref|comp⟩ after undoing the final layout; `why` is set when classical results differ.
    Result<num::Complex> run(const std::vector<num::Complex>& input, std::uint64_t plan,
                             std::string& why) const {
        StateVec a(static_cast<std::uint32_t>(m.active.size())), b(m.compWires);
        a.amplitudes() = input;
        std::fill(b.amplitudes().begin(), b.amplitudes().end(), num::Complex{});
        for (std::size_t y = 0; y < input.size(); ++y)
            b.amplitudes()[scatter(y, m, m.start)] = input[y];
        QXL_TRY_ASSIGN(const detail::SimRun ra, detail::execute(ref, a, clbits, plan));
        QXL_TRY_ASSIGN(const detail::SimRun rb, detail::execute(comp, b, clbits, plan));
        if (ra.bits != rb.bits)
            why = "the classical memories differ after the same planned outcomes";
        else if (std::abs(ra.probability - rb.probability) > 1e-7)
            why = std::format("outcome probabilities differ: {} vs {}", ra.probability,
                              rb.probability);
        num::Complex s{};
        for (std::size_t y = 0; y < input.size(); ++y)
            s += std::conj(a.amplitudes()[y]) * b.amplitudes()[scatter(y, m, m.end)];
        return s;
    }
};
} // namespace

std::string_view equivalenceMethodName(EquivalenceMethod m) {
    switch (m) {
    case EquivalenceMethod::Auto:
        return "auto";
    case EquivalenceMethod::Unitary:
        return "unitary";
    case EquivalenceMethod::RandomStates:
        return "random_states";
    case EquivalenceMethod::Clifford:
        return "clifford";
    case EquivalenceMethod::Skipped:
        return "skipped";
    }
    return "?";
}

Result<EquivalenceReport> checkEquivalence(const ir::Circuit& reference,
                                           const ir::Circuit& compiled,
                                           const EquivalenceOptions& o) {
    EquivalenceReport report;
    if (reference.clbitCount() != compiled.clbitCount())
        return fail(ErrorCode::InvalidArgument,
                    "the circuits declare different classical bit spaces");
    if (hasOpaqueGate(reference) || hasOpaqueGate(compiled)) {
        if (o.method != EquivalenceMethod::Auto)
            return fail(err::Unsupported, "a defcal-only gate has no matrix to compare");
        report.detail = "a defcal-only gate has no matrix to compare";
        return report;
    }
    QXL_TRY_ASSIGN(const Mapping m, mappingOf(reference, compiled));
    const bool pure = reference.isPureUnitary() && compiled.isPureUnitary();

    if (pure && (o.method == EquivalenceMethod::Auto || o.method == EquivalenceMethod::Clifford)) {
        const auto ta = cliffordTableauOf(reference);
        const auto tb = ta ? cliffordTableauOf(compiled) : std::nullopt;
        if (ta && tb)
            return cliffordCompare(*ta, *tb, m);
    }
    if (o.method == EquivalenceMethod::Clifford)
        return fail(err::Unsupported,
                    "the Clifford method needs two measurement-free Clifford circuits");

    QXL_TRY_ASSIGN(const SimProgram ref, detail::compileForSimulation(reference, m.refMap));
    QXL_TRY_ASSIGN(const SimProgram comp, detail::compileForSimulation(compiled, m.compMap));
    const auto kA = static_cast<std::uint32_t>(m.active.size());
    report.wires = m.compWires;
    const double perInput =
        static_cast<double>(ref.gates) * std::ldexp(1.0, static_cast<int>(kA)) +
        static_cast<double>(comp.gates) * std::ldexp(1.0, static_cast<int>(m.compWires));
    const double limit = o.workLimit == 0 ? INFINITY : static_cast<double>(o.workLimit);
    const Simulation sim{m, ref, comp, reference.clbitCount()};

    const bool unitaryFits = pure && m.compWires <= o.maxUnitaryQubits;
    if (o.method == EquivalenceMethod::Unitary && !unitaryFits)
        return fail(
            err::TooLarge,
            std::format(
                "the Unitary method takes measurement-free circuits on at most {} wires (here {})",
                o.maxUnitaryQubits, m.compWires));
    if (o.method == EquivalenceMethod::Unitary ||
        (o.method == EquivalenceMethod::Auto && unitaryFits &&
         perInput * std::ldexp(1.0, static_cast<int>(kA)) <= limit)) {
        report.method = EquivalenceMethod::Unitary;
        const std::size_t dim = std::size_t{1} << kA;
        report.inputs = static_cast<std::uint32_t>(dim);
        num::Complex trace{};
        std::vector<num::Complex> basis(dim);
        std::string why;
        for (std::size_t x = 0; x < dim; ++x) {
            std::fill(basis.begin(), basis.end(), num::Complex{});
            basis[x] = 1.0;
            QXL_TRY_ASSIGN(const num::Complex s, sim.run(basis, 0, why));
            trace += s;
        }
        report.worstOverlap = std::abs(trace) / static_cast<double>(dim); // |tr(U_a† U_b)| / 2^k
        report.equivalent = report.worstOverlap >= 1.0 - o.tolerance;
        if (!report.equivalent)
            report.detail = std::format("|tr(U_a† U_b)|/2^{} = {:.12f}", kA, report.worstOverlap);
        return report;
    }
    if (m.compWires > o.maxStateQubits) {
        if (o.method == EquivalenceMethod::RandomStates)
            return fail(err::TooLarge,
                        std::format("{} wires exceed the {}-wire cap of the random-state method",
                                    m.compWires, o.maxStateQubits));
        report.detail = std::format("{} wires exceed the {}-wire cap of the random-state method",
                                    m.compWires, o.maxStateQubits);
        return report;
    }
    const std::uint32_t branches =
        ref.measures || comp.measures ? std::max<std::uint32_t>(o.branches, 1) : 1;
    std::uint32_t states = std::max<std::uint32_t>(o.states, 1);
    if (std::isfinite(limit)) {
        const double affordable = limit / std::max(perInput * branches, 1.0);
        if (affordable < 1.0) {
            if (o.method == EquivalenceMethod::RandomStates)
                return fail(err::TooLarge, "the work limit does not allow a single random state");
            report.detail = "the work limit does not allow a single random state";
            return report;
        }
        states = static_cast<std::uint32_t>(std::min<double>(states, affordable));
    }
    report.method = EquivalenceMethod::RandomStates;
    report.equivalent = true;
    core::Random rng(o.seed);
    for (std::uint32_t s = 0; s < states && report.equivalent; ++s) {
        const std::vector<num::Complex> psi = detail::haarState(kA, rng);
        for (std::uint32_t b = 0; b < branches && report.equivalent; ++b) {
            std::string why;
            QXL_TRY_ASSIGN(const num::Complex overlap,
                           sim.run(psi, o.seed + 0x1000003ull * s + 0x7F4A7C15ull * b, why));
            ++report.inputs;
            report.worstOverlap = std::min(report.worstOverlap, std::abs(overlap));
            if (!why.empty()) {
                report.equivalent = false;
                report.detail = why;
            } else if (std::abs(overlap) < 1.0 - o.tolerance) {
                report.equivalent = false;
                report.detail = std::format("|⟨ψ|U_a† U_b|ψ⟩| = {:.12f} on random input {}",
                                            std::abs(overlap), s);
            }
        }
    }
    return report;
}

} // namespace qlab::compiler
