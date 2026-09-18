// Spec 12 §8, 13 §7 — probes: the Simulator-only quantities a program asks for at every snapshot
// boundary. Runtime evaluates them on the worker from the backend state; the instruments read the
// values, never the state (spec 02 §2 keeps Instruments off Runtime).
#include "Lang/Sema.hpp"
#include "Data/Fidelity.hpp"
#include "Numerics/Tensor.hpp"
#include "QSim/Measures.hpp"
#include "Runtime/Run.hpp"
#include <algorithm>
#include <charconv>

namespace qlab::runtime {
namespace {

// Device qubits of one `pragma qlab.probe` operand: "$3", "q[0]" or a whole register "q".
void operandQubits(std::string_view text, const lang::Program& program, const compiler::CompiledProgram& compiled,
                   std::vector<std::uint32_t>& out) {
    const auto number = [](std::string_view s) -> std::optional<std::uint32_t> {
        std::uint32_t v = 0;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        return r.ec == std::errc{} && r.ptr == s.data() + s.size() ? std::optional{v} : std::nullopt;
    };
    const auto physical = [&](std::uint32_t virtualQubit) {
        // `initialLayout` is the map the compile chose; a physical program carries its own indices.
        if (virtualQubit < compiled.initialLayout.size()) return compiled.initialLayout.physical(virtualQubit);
        return virtualQubit;
    };
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    if (text.empty()) return;
    if (text.front() == '$') {
        if (auto q = number(text.substr(1))) out.push_back(*q);
        return;
    }
    const std::size_t open = text.find('[');
    if (open == std::string_view::npos) { // a whole register
        for (const lang::QubitRegInfo& r : program.qubitRegs) {
            if (r.name != text) continue;
            for (std::size_t i = 0; i < r.size; ++i)
                if (auto v = program.qubitIndex(r.name, i)) out.push_back(physical(*v));
        }
        return;
    }
    if (text.back() != ']') return;
    const auto element = number(text.substr(open + 1, text.size() - open - 2));
    if (!element) return;
    if (auto v = program.qubitIndex(text.substr(0, open), *element)) out.push_back(physical(*v));
}

// Simulator indices of a probe's device qubits; the plan's qubits when it names none.
std::vector<QubitIndex> simQubits(const ProbeRequest& probe, const ProgramPlan& plan) {
    std::vector<QubitIndex> sim;
    if (probe.qubits.empty()) {
        for (std::uint32_t s = 0; s < plan.nQubits(); ++s) sim.push_back(QubitIndex{s});
        return sim;
    }
    for (std::uint32_t q : probe.qubits) {
        const std::int32_t s = q < plan.toSim.size() ? plan.toSim[q] : -1;
        if (s >= 0) sim.push_back(QubitIndex{static_cast<std::uint32_t>(s)});
    }
    return sim;
}

void flatten(const num::Matrix& rho, std::vector<double>& values) {
    for (std::size_t i = 0; i < rho.rows; ++i)
        for (std::size_t j = 0; j < rho.cols; ++j) {
            values.push_back(rho(i, j).real());
            values.push_back(rho(i, j).imag());
        }
}

ProbeValue header(const ProbeRequest& probe, std::span<const QubitIndex> sim) {
    ProbeValue v;
    v.kind = probe.kind;
    for (QubitIndex q : sim) v.qubits.push_back(q.get());
    v.cls = data::FidelityClass::Exact;
    return v;
}
} // namespace

std::vector<ProbeRequest> resolveProbes(const lang::Program& program, const compiler::CompiledProgram& compiled) {
    std::vector<ProbeRequest> out;
    for (const lang::Probe& p : program.pragmas.probes) {
        ProbeRequest r;
        r.kind = p.kind;
        r.atBarriers = p.atBarriers;
        for (const std::string& operand : p.qubits) operandQubits(operand, program, compiled, r.qubits);
        std::sort(r.qubits.begin(), r.qubits.end());
        r.qubits.erase(std::unique(r.qubits.begin(), r.qubits.end()), r.qubits.end());
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<ProbeValue> evaluateProbes(const qsim::IBackend& backend, const ProgramPlan& plan,
                                       std::span<const ProbeRequest> probes) {
    std::vector<ProbeValue> out;
    for (const ProbeRequest& probe : probes) {
        const std::vector<QubitIndex> sim = simQubits(probe, plan);
        if (sim.empty()) continue;
        ProbeValue v = header(probe, sim);
        if (probe.kind == "state") { // the snapshot itself carries the state (Simulator-only)
            out.push_back(std::move(v));
            continue;
        }
        qsim::SnapshotRequest req;
        req.reducedStates = true;
        if (probe.kind == "bloch")
            for (QubitIndex q : sim) req.subsystems.push_back({q});
        else
            req.subsystems.push_back(sim);
        auto snap = backend.snapshot(req);
        if (!snap || snap->reduced.size() != req.subsystems.size()) continue; // e.g. a tableau
        if (probe.kind == "bloch") {
            for (const qsim::ReducedState& r : snap->reduced) {
                auto b = qsim::measures::blochVector(r.rho);
                if (!b) { v.values.insert(v.values.end(), {0.0, 0.0, 0.0}); continue; }
                v.values.insert(v.values.end(), b->begin(), b->end());
            }
        } else if (probe.kind == "entanglement") {
            v.values.push_back(qsim::measures::entropyBits(snap->reduced.front().rho));
            v.values.push_back(qsim::measures::purity(snap->reduced.front().rho));
        } else if (probe.kind == "density") {
            flatten(snap->reduced.front().rho, v.values);
        } else {
            continue; // an unknown probe kind is not invented here (spec 13 §7 lists them)
        }
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<ProbeValue> evaluateProbes(const num::Matrix& rho, std::uint32_t nQubits, const ProgramPlan& plan,
                                       std::span<const ProbeRequest> probes) {
    std::vector<ProbeValue> out;
    const std::vector<std::size_t> dims(nQubits, 2);
    for (const ProbeRequest& probe : probes) {
        const std::vector<QubitIndex> sim = simQubits(probe, plan);
        if (sim.empty()) continue;
        ProbeValue v = header(probe, sim);
        if (probe.kind == "state") {
            out.push_back(std::move(v));
            continue;
        }
        const auto reduced = [&](std::span<const QubitIndex> keep) {
            std::vector<std::size_t> idx;
            for (QubitIndex q : keep)
                if (q.get() < nQubits) idx.push_back(q.get());
            std::sort(idx.begin(), idx.end());
            return num::partialTrace(rho, dims, idx);
        };
        if (probe.kind == "bloch") {
            for (QubitIndex q : sim) {
                auto b = qsim::measures::blochVector(reduced({&q, 1}));
                if (!b) { v.values.insert(v.values.end(), {0.0, 0.0, 0.0}); continue; }
                v.values.insert(v.values.end(), b->begin(), b->end());
            }
        } else if (probe.kind == "entanglement") {
            const num::Matrix r = reduced(sim);
            v.values.push_back(qsim::measures::entropyBits(r));
            v.values.push_back(qsim::measures::purity(r));
        } else if (probe.kind == "density") {
            flatten(reduced(sim), v.values);
        } else {
            continue;
        }
        v.cls = data::FidelityClass::Numerical; // from an integrated ρ (spec 07 §5)
        out.push_back(std::move(v));
    }
    return out;
}

} // namespace qlab::runtime
