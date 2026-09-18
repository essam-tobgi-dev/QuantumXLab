// Spec 08 §4, §7.4 — what the compiler/runtime asks the model for: the channels of one operation,
// of one idle interval, of a scheduled slot's crosstalk, of a readout, the readout map and the
// collapse operators.
#include "Noise/Model.hpp"
#include "Numerics/Tensor.hpp"
#include <algorithm>
#include <format>

namespace qlab::noise {
namespace {
constexpr std::string_view kVirtual[] = {"rz", "id", "barrier", "delay"};
bool isVirtual(std::string_view g) { return std::find(std::begin(kVirtual), std::end(kVirtual), g) != std::end(kVirtual); }

void push(std::vector<AttachedChannel>& out, const ChannelPtr& c, Placement p, std::vector<QubitIndex> qubits, double durationS) {
    if (c) out.push_back(AttachedChannel{c, p, std::move(qubits), Context{durationS, 0.0, false}});
}
bool contains(std::span<const QubitIndex> set, std::uint32_t q) {
    return std::find(set.begin(), set.end(), QubitIndex{q}) != set.end();
}
} // namespace

std::vector<AttachedChannel> NoiseModel::channelsFor(std::string_view gate, std::span<const QubitIndex> qubits) const {
    std::vector<AttachedChannel> out;
    if (qubits.empty() || isVirtual(gate) || gate == "measure") return out; // readout is classical (§3)
    for (auto q : qubits)
        if (q.get() >= qubitChannels_.size()) return out;
    if (gate == "reset") { // spec 08 §5.5: residual excited population after an active reset
        if (!overrides_.disabled(id::ResetError))
            for (auto q : qubits) push(out, qubitChannels_[q.get()].reset, Placement::After, {q}, 0.0);
        return out;
    }
    const GateEntry* entry = findGate(gate, qubits);
    if (!entry || entry->noise.qubits.size() != qubits.size()) return out;
    const double t = entry->noise.durationS;
    if (t > 0.0) { // T04 §6 step 2, and the quasi-static detuning during gates (spec 08 §2.4)
        if (!overrides_.disabled(id::ThermalRelaxation))
            for (auto q : qubits) push(out, qubitChannels_[q.get()].relaxation, Placement::After, {q}, t);
        if (!overrides_.disabled(id::DetuningDrift))
            for (auto q : qubits) push(out, qubitChannels_[q.get()].drift, Placement::During, {q}, t);
    }
    const std::vector<QubitIndex> targets(qubits.begin(), qubits.end());
    if (!overrides_.disabled(qubits.size() == 1 ? id::Depolarizing1q : id::Depolarizing2q))
        push(out, entry->depolarizing, Placement::After, targets, t);
    if (!overrides_.disabled(id::OverRotation)) // axis in query order: Z on this call's control
        push(out, entry->overRotation, Placement::After, targets, t);
    if (levels_ == 3 && !overrides_.disabled(id::Leakage))
        for (auto q : qubits) push(out, entry->leakage, Placement::After, {q}, t);
    return out;
}

std::vector<AttachedChannel> NoiseModel::idleChannels(QubitIndex q, double durationS) const {
    std::vector<AttachedChannel> out;
    if (!(durationS > 0.0) || q.get() >= qubitChannels_.size()) return out;
    const QubitChannels& c = qubitChannels_[q.get()];
    if (!overrides_.disabled(id::ThermalRelaxation)) push(out, c.relaxation, Placement::During, {q}, durationS);
    if (!overrides_.disabled(id::DetuningDrift)) push(out, c.drift, Placement::During, {q}, durationS);
    return out;
}

std::vector<AttachedChannel> NoiseModel::idleChannels(QubitIndex q, Picoseconds dt) const {
    return idleChannels(q, static_cast<double>(dt.get()) * 1e-12);
}

std::vector<AttachedChannel> NoiseModel::crosstalkChannels(double durationS, std::span<const QubitIndex> inTwoQubitGates) const {
    std::vector<AttachedChannel> out;
    if (!(durationS > 0.0) || overrides_.disabled(id::ZzCrosstalk)) return out;
    for (const auto& [key, e] : edges_) {
        if (!e.zz) continue;
        // Spec 08 §2.4: while either qubit is idle or under a single-qubit gate. The calibrated
        // two-qubit gate on the pair itself already contains its ZZ.
        if (contains(inTwoQubitGates, e.noise.a) && contains(inTwoQubitGates, e.noise.b)) continue;
        push(out, e.zz, Placement::During, {QubitIndex{e.noise.a}, QubitIndex{e.noise.b}}, durationS);
    }
    return out;
}

std::vector<AttachedChannel> NoiseModel::measurementChannels(std::span<const QubitIndex> measured) const {
    std::vector<AttachedChannel> out;
    if (overrides_.disabled(id::MeasurementDephasing)) return out;
    for (const auto& line : feedlines_) {
        double window = 0.0; // the readout pulse of the line lasts as long as its longest measurement
        for (auto q : measured)
            if (q.get() < qubits_.size() && std::find(line.begin(), line.end(), q.get()) != line.end())
                window = std::max(window, qubits_[q.get()].readoutDurationS);
        if (!(window > 0.0)) continue;
        for (std::uint32_t q : line)
            if (q < qubitChannels_.size() && !contains(measured, q))
                push(out, qubitChannels_[q].measurement, Placement::During, {QubitIndex{q}}, window);
    }
    return out;
}

std::vector<AttachedChannel> NoiseModel::preparationChannels(std::span<const QubitIndex> qubits) const {
    std::vector<AttachedChannel> out;
    if (overrides_.disabled(id::ThermalPreparation)) return out;
    for (auto q : qubits)
        if (q.get() < qubitChannels_.size()) push(out, qubitChannels_[q.get()].preparation, Placement::Before, {q}, 0.0);
    return out;
}

Result<ReadoutModel> NoiseModel::readout(std::span<const QubitIndex> qubits) const {
    std::vector<QubitIndex> list(qubits.begin(), qubits.end());
    for (auto q : list)
        if (q.get() >= qubits_.size()) return fail(err::UnknownTarget, std::format("qubit {} has no readout calibration", q.get()));
    if (overrides_.disabled(id::Readout)) return ReadoutModel::ideal(std::move(list));
    std::vector<ReadoutFactor> factors;
    std::vector<bool> covered(list.size(), false);
    for (const auto& group : readoutGroups_) { // a correlated group replaces the tensor product (§3)
        if (!group.assignment) continue;
        std::vector<std::size_t> positions;
        for (std::uint32_t q : group.qubits) {
            auto it = std::find(list.begin(), list.end(), QubitIndex{q});
            if (it == list.end()) break;
            positions.push_back(static_cast<std::size_t>(it - list.begin()));
        }
        if (positions.size() != group.qubits.size()) continue; // group not fully measured: per-qubit matrices
        if (std::any_of(positions.begin(), positions.end(), [&](std::size_t p) { return covered[p]; })) continue;
        for (auto p : positions) covered[p] = true;
        factors.push_back(ReadoutFactor{std::move(positions), *group.assignment});
    }
    for (std::size_t k = 0; k < list.size(); ++k)
        if (!covered[k]) factors.push_back(ReadoutFactor{{k}, toRealMatrix(qubits_[list[k].get()].readoutAssignment)});
    return ReadoutModel::make(std::move(list), std::move(factors));
}

Result<std::vector<qsim::CollapseOp>> NoiseModel::lindbladOperators(std::span<const QubitIndex> qubits,
                                                                     std::span<const std::uint32_t> siteDims) const {
    if (qubits.size() != siteDims.size())
        return fail(err::BadDimensions, std::format("{} simulated qubits but {} site dimensions", qubits.size(), siteDims.size()));
    std::vector<qsim::CollapseOp> out;
    if (overrides_.disabled(id::ThermalRelaxation)) return out;
    std::vector<num::Matrix> factors; // kronList: element k acts on site k (little-endian, T01 §4)
    for (std::uint32_t d : siteDims) factors.push_back(num::Matrix::identity(d));
    for (std::size_t site = 0; site < qubits.size(); ++site) {
        const std::uint32_t q = qubits[site].get();
        if (q >= qubits_.size()) return fail(err::UnknownTarget, std::format("site {} simulates qubit {}, which is not in the model", site, q));
        const QubitNoise& qn = qubits_[q];
        // √((1−p_th)/T1)·a, √(p_th/T1)·a†, √(γφ/2)·Z or √(2γφ)·a†a (spec 08 (7.2), T04 (3.3), (4.1)).
        QXL_TRY_ASSIGN(auto terms, thermalRelaxationLindblad(qn.t1S, qn.t2S, qn.pThermal, siteDims[site]));
        for (auto& term : terms) {
            auto embedded = factors;
            embedded[site] = std::move(term.op);
            out.push_back(qsim::CollapseOp{std::format("{} q{}", term.name, q), num::kronList(embedded)});
        }
    }
    return out;
}

Result<std::vector<qsim::CollapseOp>> NoiseModel::lindbladOperators(std::span<const std::uint32_t> siteDims) const {
    std::vector<QubitIndex> qubits;
    for (std::uint32_t k = 0; k < siteDims.size(); ++k) qubits.push_back(QubitIndex{k});
    return lindbladOperators(qubits, siteDims);
}

} // namespace qlab::noise
