// Spec 15 §7 (i), §8 / T12 (4.1)–(4.2), (3.1)–(3.2), §6 — the fast product fidelity, the classical
// simulation cost on this host, and the fault-tolerant row.
#include "Compiler/Pass.hpp"
#include "Data/Fidelity.hpp"
#include "QSim/Backend.hpp"
#include "Runtime/Estimate.hpp"
#include "Runtime/Plan.hpp"
#include <chrono>
#include <cmath>
#include <complex>
#include <format>
#include <vector>

namespace qlab::runtime {
namespace {

// Gate instances of the compiled circuit with the physical qubits they act on, bodies included.
void gateInstances(const ir::Circuit& c, std::vector<std::pair<std::string, std::vector<std::uint32_t>>>& out) {
    for (ir::NodeId id : c.topologicalOrder()) {
        const ir::Node& n = c.node(id);
        if (const auto* g = std::get_if<ir::Gate>(&n)) {
            std::vector<std::uint32_t> qs;
            for (ir::Wire w : g->wires()) qs.push_back(w.index);
            out.emplace_back(g->name, std::move(qs));
        } else if (const auto* b = std::get_if<ir::Branch>(&n)) {
            if (b->thenBody.present()) gateInstances(*b->thenBody, out);
            if (b->elseBody.present()) gateInstances(*b->elseBody, out);
        } else if (const auto* l = std::get_if<ir::Loop>(&n)) {
            if (l->body.present()) gateInstances(*l->body, out);
        } else if (const auto* bx = std::get_if<ir::Box>(&n)) {
            if (bx->body.present()) gateInstances(*bx->body, out);
        }
    }
}

// T12 §7: value ∓ 2σ for the favourable and unfavourable evaluations of (7.1).
struct Ends { double typ = 1.0, low = 1.0, high = 1.0; };
void multiply(Ends& e, double value, double sigma) {
    e.typ *= 1.0 - value;
    e.low *= std::clamp(1.0 - (value + 2.0 * sigma), 0.0, 1.0);   // unfavourable end of the fidelity
    e.high *= std::clamp(1.0 - std::max(0.0, value - 2.0 * sigma), 0.0, 1.0);
}
} // namespace

Result<FidelityEstimate> estimateFidelityFast(const EstimateInput& in) {
    if (!in.device || !in.calibration || !in.program)
        return fail(err::Unsupported, "the fidelity estimate needs a device, a calibration and a compiled program");
    const hw::Calibration& cal = *in.calibration;
    FidelityEstimate f;
    Ends gates, idle, readout;
    double varLog = 0.0;

    // Π_g (1 − ε_g): the RB average error of every gate instance on the physical qubits it runs on.
    std::vector<std::pair<std::string, std::vector<std::uint32_t>>> instances;
    gateInstances(in.program->circuit, instances);
    for (const auto& [name, qubits] : instances) {
        if (name == "rz" || name == "id" || name == "i" || name == "barrier") continue; // virtual / free
        auto e = cal.gateError(name, qubits);
        if (!e) continue;
        double sigma = 0.0;
        if (qubits.size() == 1) {
            if (const hw::QubitCal* q = cal.qubit(qubits[0])) sigma = q->gateError1q.sigma;
        } else if (qubits.size() == 2) {
            if (const hw::EdgeCal* edge = cal.edge(qubits[0], qubits[1])) sigma = edge->gateError2q.sigma;
        }
        multiply(gates, *e, sigma);
        if (*e < 1.0) varLog += (sigma / (1.0 - *e)) * (sigma / (1.0 - *e)); // T12 §7 log-domain band
    }

    // Π_(q,Δt) (1 − ½(1 − e^{−Δt/T1}) − ½(1 − e^{−Δt/Tφ})): the idle intervals of the schedule.
    for (const auto& [wire, intervals] : in.program->timing.idle) {
        const hw::QubitCal* q = cal.qubit(wire);
        if (!q) continue;
        const double t1 = q->t1.value.v, tphi = q->tphi().v;
        for (const compiler::IdleInterval& iv : intervals) {
            const double dt = static_cast<double>(iv.length().get()) * 1e-12;
            if (dt <= 0.0) continue;
            const double eps = 0.5 * (1.0 - std::exp(-dt / std::max(t1, 1e-30))) +
                               0.5 * (1.0 - std::exp(-dt / std::max(tphi, 1e-30)));
            multiply(idle, eps, 0.0);
        }
    }

    // Π_(q measured) F_ro(q).
    for (std::uint32_t q : in.measuredQubits) {
        const hw::QubitCal* c = cal.qubit(q);
        if (!c) continue;
        multiply(readout, 1.0 - c->readoutFidelity(), 0.0);
    }

    f.gateProduct = gates.typ;
    f.idleProduct = idle.typ;
    f.readoutProduct = readout.typ;
    f.fast = gates.typ * idle.typ * readout.typ;
    f.low = gates.low * idle.low * readout.low;
    f.high = gates.high * idle.high * readout.high;
    f.sigmaLog = std::sqrt(varLog);
    f.cls = data::FidelityClass::Model;
    f.caveats = {"errors are treated as independent and multiplicative: no coherent build-up, no crosstalk",
                 "gate errors are average (RB) fidelities, so state-dependent errors are ignored",
                 "the idle factor assumes an equal superposition: worst case for dephasing, average for relaxation",
                 "this is a success-probability proxy, not a state fidelity"};
    return f;
}

double hostGateCost() {
    // T12 §3: seconds per amplitude update on THIS host, measured once per process. Spec 15 §8 asks
    // for a 1 s benchmark; a 2^18-amplitude sweep repeated for ≈ 15 ms reaches the same figure to
    // the digit the panel shows without stalling every process start (SPEC_DEVIATIONS.md).
    static const double cost = [] {
        constexpr std::size_t kN = 1u << 18;
        std::vector<std::complex<double>> v(kN, std::complex<double>{1.0, 0.0});
        const auto t0 = std::chrono::steady_clock::now();
        std::uint64_t updates = 0;
        double elapsed = 0.0;
        const std::complex<double> a{0.70710678118654752, 0.0};
        do {
            for (std::size_t i = 0; i + 1 < kN; i += 2) { // one two-amplitude butterfly, as a 1q kernel does
                const std::complex<double> x = v[i], y = v[i + 1];
                v[i] = a * (x + y);
                v[i + 1] = a * (x - y);
            }
            updates += kN;
            elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        } while (elapsed < 0.015);
        // Keep the work observable so the loop is not optimized away.
        return v[0].real() != 0.0 || v[0].real() == 0.0 ? elapsed / static_cast<double>(updates) : 0.0;
    }();
    return cost;
}

ClassicalCost classicalCost(std::uint32_t qubits, std::uint64_t gates, double perShotS, std::uint64_t shots) {
    ClassicalCost c;
    const double n = qubits;
    c.stateVectorBytes = 16.0 * std::pow(2.0, n);
    c.densityMatrixBytes = 16.0 * std::pow(4.0, n);
    c.stabilizerBytes = std::max(64.0, 2.0 * n * (2.0 * n + 1.0) / 8.0); // O(n²) bits (spec 15 §8)
    c.gateCostS = hostGateCost();
    c.estimatedTimeS = static_cast<double>(gates) * std::pow(2.0, n) * c.gateCostS;
    c.hostMaxQubits = qsim::maxQubitsFor(16, false);
    // T12 (3.2): n* where the simulation costs as much as the hardware shots would.
    if (gates > 0 && shots > 0 && perShotS > 0.0) {
        const double ratio = static_cast<double>(shots) * perShotS / (c.gateCostS * static_cast<double>(gates));
        if (ratio > 0.0) c.crossoverQubits = std::log2(ratio);
    }
    return c;
}

Result<qec::ResourceEstimate> estimateQec(const EstimateInput& in, const ResourceSummary& res) {
    if (!in.device || !in.calibration) return fail(err::Unsupported, "the QEC estimate needs a device");
    // Spec 15 §8: the device's physical error rate is the mean two-qubit error over the edges the
    // program used (all calibrated edges when it used none).
    double sum = 0.0;
    std::size_t n = 0;
    for (const auto& [key, edge] : in.calibration->edges) {
        const bool used = in.usedQubits.empty() ||
                          (std::find(in.usedQubits.begin(), in.usedQubits.end(), edge.a) != in.usedQubits.end() &&
                           std::find(in.usedQubits.begin(), in.usedQubits.end(), edge.b) != in.usedQubits.end());
        if (!used) continue;
        sum += edge.gateError2q.value;
        ++n;
    }
    if (n == 0)
        for (const auto& [key, edge] : in.calibration->edges) {
            sum += edge.gateError2q.value;
            ++n;
        }
    if (n == 0) return fail(err::Unsupported, "the calibration has no two-qubit gate errors");

    qec::ResourceInput q;
    q.physicalErrorRate = sum / static_cast<double>(n);
    q.cycleTimeS = in.device->control.qecCycleTime.v;
    q.logicalQubits = std::max<double>(1.0, res.qubits);
    q.tCount = res.tCount;
    q.cliffordDepth = res.depth;
    q.failureBudget = in.qecFailureBudget;
    return qec::ResourceEstimator{}.estimate(q);
}

Result<Estimate> estimate(const EstimateInput& in) {
    if (!in.device || !in.calibration || !in.program)
        return fail(err::Unsupported, "an estimate needs a device, a calibration and a compiled program");
    Estimate e;
    e.device = in.device->id;
    e.calibrationTimestamp = in.calibration->timestamp;
    e.cls = data::FidelityClass::Model;
    QXL_TRY_ASSIGN(e.wallTime, estimateWallTime(in));
    QXL_TRY_ASSIGN(e.fidelity, estimateFidelityFast(in));
    e.resources = summarizeResources(in);
    e.classicalCost = classicalCost(e.resources.qubits, in.program->metrics.gateCount, e.wallTime.perShotS, in.shots);

    e.assumptions = {"queue_time_excluded",
                     std::format("reset_policy:{}", e.wallTime.resetPolicy == hw::ResetPolicy::Active     ? "active"
                                                    : e.wallTime.resetPolicy == hw::ResetPolicy::Passive  ? "passive"
                                                                                                          : "cooling"),
                     "independent_errors",
                     "average_gate_fidelities",
                     "no_crosstalk",
                     "calibration_static",
                     "asap_critical_path",
                     "single_job_no_batching",
                     "classical_bandwidth_model"};
    // Spec 15 §8 / T12 §6: the fault-tolerant row appears once the uncorrected fidelity falls below
    // the threshold; it is never blended with the NISQ estimate.
    if (in.includeQec && e.fidelity.fast < in.qecThreshold) {
        if (auto q = estimateQec(in, e.resources)) {
            e.qec = std::move(*q);
            e.assumptions.emplace_back("qec_surface_code");
        }
    }
    return e;
}

} // namespace qlab::runtime
