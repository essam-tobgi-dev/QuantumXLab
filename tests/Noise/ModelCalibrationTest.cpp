// Spec 08 §4.1, §9 / spec 25 §3.4 — NoiseModel::fromCalibration on the shipped devices: every field
// mapped, the error budget p_dep = d r/(d−1) − p_relax (clamped at 0 with a warning), and the
// queries.
#include "Hardware/Hardware.hpp"
#include "NoiseTestSupport.hpp"
#include <algorithm>
#include <catch2/catch_approx.hpp>

using namespace ntest;
using namespace qlab::noise;
using Catch::Approx;

namespace {
hw::LoadedDevice device(std::string_view id) {
    auto d = hw::loadShippedDevice(id);
    NOISE_REQUIRE_OK(d);
    return std::move(*d);
}
// Depolarizing equivalent of thermal relaxation computed through the Kraus operators themselves
// (tensor product, T10 (1.2)–(1.5)): independent of the closed form the model uses.
double krausRelaxation(const hw::Calibration& cal, std::span<const std::uint32_t> qs, double t) {
    std::optional<Kraus> joint;
    for (std::uint32_t qb : qs) {
        const auto& c = cal.qubits[qb];
        auto k = channels::thermalRelaxation(c.t1.value.si(),
                                             std::min(c.t2echo.value.si(), 2.0 * c.t1.value.si()),
                                             t, c.thermalPopulation.value);
        NOISE_REQUIRE_OK(k);
        if (!joint) {
            joint = std::move(*k);
            continue;
        }
        auto both = tensor(*joint, *k);
        NOISE_REQUIRE_OK(both);
        joint = std::move(*both);
    }
    return depolarizingEquivalent(*joint);
}
std::vector<std::string_view> ids(const std::vector<AttachedChannel>& list) {
    std::vector<std::string_view> out;
    for (const auto& a : list)
        out.push_back(a.channel->id());
    return out;
}
double weightOf(const AttachedChannel& a, std::size_t pauliIndex) {
    auto k = a.channel->kraus(a.context);
    NOISE_REQUIRE_OK(k);
    for (std::size_t n = 0; n < k->ops.size(); ++n)
        if (k->pauliIndices[n] == pauliIndex)
            return k->pauliWeights[n];
    return 0.0;
}
} // namespace

TEST_CASE("fromCalibration maps every sc_fixed_5 calibration field (spec 08 §4.1)") {
    const auto dev = device("sc_fixed_5");
    auto built = NoiseModel::fromCalibration(dev.device, dev.calibration);
    NOISE_REQUIRE_OK(built);
    const NoiseModel& m = *built;
    REQUIRE(m.warnings().empty());
    REQUIRE(m.qubitCount() == 5);
    REQUIRE(m.deviceId() == "sc_fixed_5");
    REQUIRE(m.calibrationStamp() == dev.calibration.timestamp);
    for (std::uint32_t qb = 0; qb < 5; ++qb) {
        const auto& c = dev.calibration.qubits[qb];
        const QubitNoise* qn = m.qubit(QubitIndex{qb});
        REQUIRE(qn);
        REQUIRE(qn->t1S == Approx(c.t1.value.si()).epsilon(1e-15));
        REQUIRE(qn->t2S == Approx(c.t2echo.value.si()).epsilon(1e-15));
        REQUIRE(qn->t2StarS == Approx(c.t2star.value.si()).epsilon(1e-15));
        REQUIRE(qn->driftSigmaHz ==
                Approx(driftSigmaFromT2Star(c.t2star.value.si())).epsilon(1e-14)); // T2* < T2
        REQUIRE(qn->frequencyHz == Approx(c.f01.value.si()).epsilon(1e-15));
        REQUIRE(qn->pThermal == c.thermalPopulation.value);
        REQUIRE(qn->resetError == c.resetError.value);
        REQUIRE(qn->readoutAssignment == c.readoutAssignment);
        REQUIRE(qn->readoutDurationS == Approx(700e-9).epsilon(1e-15));

        const std::uint32_t target[] = {qb};
        const GateNoise* sx = m.gate("sx", q({qb}));
        REQUIRE(sx);
        REQUIRE(sx->errorR == c.gateError1q.value);
        REQUIRE(sx->durationS == Approx(32e-9).epsilon(1e-15));
        REQUIRE(sx->pTotal == Approx(2.0 * c.gateError1q.value).epsilon(1e-15));
        REQUIRE(sx->pRelaxation ==
                Approx(krausRelaxation(dev.calibration, target, 32e-9)).epsilon(1e-9));
        REQUIRE(sx->depolarizing == Approx(sx->pTotal - sx->pRelaxation).epsilon(1e-15));
        REQUIRE_FALSE(sx->clamped);
        const auto chans = m.channelsFor("sx", q({qb}));
        REQUIRE(ids(chans) == std::vector<std::string_view>{id::ThermalRelaxation,
                                                            id::DetuningDrift, id::Depolarizing1q});
        REQUIRE(chans[0].placement == Placement::After);
        REQUIRE(chans[0].context.durationS == Approx(32e-9).epsilon(1e-15));
        REQUIRE(chans[1].placement == Placement::During);
        REQUIRE(weightOf(chans[2], 1) == Approx(sx->depolarizing / 4.0).epsilon(1e-14)); // p/4 on X
        // Total infidelity of the attached channels reproduces the calibrated r to first order (T04
        // (6.1)): F_e(dep ∘ E) = (1 − p)F_e(E) + p/4 exactly, with 1 − F_e(E) = 3 p_relax/4.
        const double feTotal =
            (1.0 - sx->depolarizing) * (1.0 - 0.75 * sx->pRelaxation) + sx->depolarizing / 4.0;
        REQUIRE(2.0 / 3.0 * (1.0 - feTotal) == Approx(c.gateError1q.value).epsilon(1e-3));
    }
    for (const auto& [key, e] : dev.calibration.edges) {
        const std::uint32_t pair[] = {e.a, e.b};
        for (const char* gate : {"cx", "ecr"}) {
            const GateNoise* g = m.gate(gate, q({e.a, e.b}));
            REQUIRE(g);
            REQUIRE(m.gate(gate, q({e.b, e.a})) == g); // either direction finds the calibrated edge
            REQUIRE(g->pTotal == Approx(4.0 * e.gateError2q.value / 3.0).epsilon(1e-15));
            REQUIRE(g->pRelaxation ==
                    Approx(krausRelaxation(dev.calibration, pair, e.duration.value.si()))
                        .epsilon(1e-9));
            for (const auto& order : {q({e.a, e.b}), q({e.b, e.a})}) {
                const auto chans = m.channelsFor(gate, order);
                const auto names = ids(chans);
                REQUIRE(std::count(names.begin(), names.end(), id::Depolarizing2q) ==
                        1); // flipped key too
                const auto dep = std::find_if(chans.begin(), chans.end(), [](const auto& a) {
                    return a.channel->id() == id::Depolarizing2q;
                });
                REQUIRE(dep->qubits == order);
                REQUIRE(weightOf(*dep, 5) == Approx(g->depolarizing / 16.0).epsilon(1e-13)); // XX
            }
        }
        REQUIRE(m.edge(e.b, e.a)->zzHz == Approx(e.zz.value.si()).epsilon(1e-15));
    }
    // ZZ during a slot: every edge unless both ends are inside two-qubit gates (spec 08 §2.4).
    REQUIRE(m.crosstalkChannels(1e-6).size() == 4);
    const auto busy = q({0, 1});
    const auto slot = m.crosstalkChannels(1e-6, busy);
    REQUIRE(slot.size() == 3);
    for (const auto& a : slot)
        REQUIRE_FALSE((a.qubits[0].get() <= 1 && a.qubits[1].get() <= 1));
    // Virtual gates, measurement, reset and state preparation.
    REQUIRE(m.channelsFor("rz", q({0})).empty());
    REQUIRE(m.channelsFor("measure", q({0})).empty());
    REQUIRE(ids(m.channelsFor("reset", q({2}))) == std::vector<std::string_view>{id::ResetError});
    REQUIRE(weightOf(m.channelsFor("reset", q({2}))[0], 1) ==
            Approx(dev.calibration.qubits[2].resetError.value).epsilon(1e-15));
    REQUIRE(ids(m.preparationChannels(q({3}))) ==
            std::vector<std::string_view>{id::ThermalPreparation});
    REQUIRE(m.preparationChannels(q({3}))[0].placement == Placement::Before);
    REQUIRE(ids(m.idleChannels(QubitIndex{4}, Picoseconds{250000})) ==
            std::vector<std::string_view>{id::ThermalRelaxation, id::DetuningDrift});
    REQUIRE(m.idleChannels(QubitIndex{4}, Picoseconds{250000})[0].context.durationS ==
            Approx(250e-9).epsilon(1e-15));
    REQUIRE(m.measurementChannels(q({0})).empty()); // readout_crosstalk_dephasing defaults to 0
    // An uncalibrated gate name uses the device default of its arity (spec 08 §8).
    REQUIRE(m.gate("h", q({1})) == m.gate("x", q({1})));
    REQUIRE_FALSE(m.isPauliOnly());
    // Readout uses the calibration matrices as stored, bits in the requested order.
    auto ro = m.readout(q({2, 0}));
    NOISE_REQUIRE_OK(ro);
    auto full = ro->fullMatrix();
    NOISE_REQUIRE_OK(full);
    const auto& m2 = dev.calibration.qubits[2].readoutAssignment;
    const auto& m0 = dev.calibration.qubits[0].readoutAssignment;
    REQUIRE((*full)(1, 2) ==
            Approx(m2[1][0] * m0[0][1]).epsilon(1e-15)); // prepared q2=1,q0=0 → read q2=0,q0=1
}

TEST_CASE("fromCalibration on sc_heavyhex_27 clamps where relaxation exceeds the gate error, with "
          "a warning") {
    const auto dev = device("sc_heavyhex_27");
    auto built = NoiseModel::fromCalibration(dev.device, dev.calibration);
    NOISE_REQUIRE_OK(built); // never an error (spec 08 §4.1)
    NoiseModel& m = *built;
    std::size_t clamps = 0;
    for (const GateNoise* g : m.gates()) {
        const double expected = krausRelaxation(dev.calibration, g->qubits, g->durationS);
        INFO(g->gate << " on " << g->qubits[0]);
        REQUIRE(g->pRelaxation == Approx(expected).epsilon(1e-9));
        REQUIRE(g->clamped == (expected > g->pTotal));
        if (g->clamped) {
            ++clamps;
            REQUIRE(g->depolarizing == 0.0);
        }
    }
    REQUIRE(clamps == 2); // cx and ecr on the edge 14-16
    REQUIRE(m.warnings().size() == 2);
    const auto has = [&](std::string_view text) {
        return std::any_of(m.warnings().begin(), m.warnings().end(),
                           [&](const std::string& w) { return w.find(text) != std::string::npos; });
    };
    REQUIRE(has("warn: relaxation alone exceeds reported gate error for cx on 14-16"));
    REQUIRE(has("warn: relaxation alone exceeds reported gate error for ecr on 14-16"));
    for (const auto& order : {q({14, 16}), q({16, 14})}) {
        const auto chans = ids(m.channelsFor("cx", order));
        REQUIRE(std::count(chans.begin(), chans.end(), id::Depolarizing2q) == 0);
        REQUIRE(std::count(chans.begin(), chans.end(), id::ThermalRelaxation) == 2);
    }
    // Scaling every gate error down makes relaxation dominate almost everywhere: warnings, no
    // error.
    Overrides o;
    o.scaleGateError = 0.05;
    NOISE_REQUIRE_OK(m.setOverrides(o));
    std::size_t clampedNow = 0;
    for (const GateNoise* g : m.gates())
        clampedNow += g->clamped;
    REQUIRE(clampedNow > 50);
    REQUIRE(m.warnings().size() == clampedNow);
    REQUIRE(std::all_of(m.warnings().begin(), m.warnings().end(),
                        [](const std::string& w) { return w.starts_with("warn: "); }));
}

TEST_CASE("fromCalibration loads every shipped device without error diagnostics (spec 08 §9)") {
    const auto all = hw::shippedDeviceIds();
    REQUIRE(all.size() >= 6);
    for (const auto& deviceId : all) {
        INFO(deviceId);
        const auto dev = device(deviceId);
        for (std::uint32_t levels : {2u, 3u}) {
            NoiseOptions options;
            options.levels = levels;
            auto m = NoiseModel::fromCalibration(dev.device, dev.calibration, options);
            NOISE_REQUIRE_OK(m);
            for (const auto& w : m->warnings())
                REQUIRE(w.starts_with("warn: "));
            REQUIRE(m->qubitCount() == dev.device.qubitCount());
            const std::uint32_t first = dev.device.dataQubits().front();
            REQUIRE_FALSE(m->idleChannels(QubitIndex{first}, 1e-6).empty());
            for (std::uint32_t c = 0; c < dev.device.qubitCount(); ++c)
                if (dev.device.isCoupler(c))
                    REQUIRE(m->idleChannels(QubitIndex{c}, 1e-6).empty()); // noiseless slot
        }
    }
    // With three levels the leakage channel is attached per qubit of a calibrated gate (spec 08
    // §2.5).
    const auto dev = device("sc_fixed_5");
    NoiseOptions three;
    three.levels = 3;
    auto m = NoiseModel::fromCalibration(dev.device, dev.calibration, three);
    NOISE_REQUIRE_OK(m);
    const auto chans = ids(m->channelsFor("cx", q({1, 2})));
    REQUIRE(std::count(chans.begin(), chans.end(), id::Leakage) == 2);
    REQUIRE(m->gate("cx", q({1, 2}))->seepage ==
            m->gate("cx", q({1, 2}))->leakage); // p_S = p_L unless given
}
