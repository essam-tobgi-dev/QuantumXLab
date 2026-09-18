// Spec 08 §4.2, §3, §7.3, T08 §7 — overrides (scale, replace, disable), the line-photon floor on the
// thermal population, measurement-induced dephasing on feedline neighbours, Pauli-only eligibility.
#include "NoiseTestSupport.hpp"
#include <catch2/catch_approx.hpp>
#include <algorithm>

using namespace ntest;
using namespace qlab::noise;
using Catch::Approx;

namespace {
NoiseModel labModel() {
    auto m = NoiseModel::fromJson(core::Json::parse(R"({
      "qubits": {
        "0": {"t1_us": 100, "t2_us": 120, "t2_star_us": 40, "p_thermal": 0.01,
              "readout": {"assignment": [[0.97, 0.03], [0.05, 0.95]], "duration_ns": 500}},
        "1": {"t1_us": 60, "t2_us": 50, "t2_star_us": 50, "p_thermal": 0.002,
              "readout": {"assignment": [[0.98, 0.02], [0.06, 0.94]], "duration_ns": 800, "crosstalk_dephasing": 0.5}}},
      "gates": {"sx": {"0": {"error": 4e-4, "duration_ns": 32}, "1": {"error": 5e-4, "duration_ns": 32}},
                "cx": {"0-1": {"error": 1.2e-2, "duration_ns": 400, "coherent_fraction": 0.2}}},
      "edges": {"0-1": {"zz_hz": 60e3}},
      "feedlines": [[0, 1]],
      "fallback_gates": {"one_qubit": "sx", "two_qubit": "cx"}
    })"));
    NOISE_REQUIRE_OK(m);
    return std::move(*m);
}
std::vector<std::string_view> ids(const std::vector<AttachedChannel>& list) {
    std::vector<std::string_view> out;
    for (const auto& a : list) out.push_back(a.channel->id());
    return out;
}
bool contains(const std::vector<std::string_view>& v, std::string_view x) { return std::find(v.begin(), v.end(), x) != v.end(); }
} // namespace

TEST_CASE("overrides scale, replace and switch off channel classes, transactionally") {
    NoiseModel m = labModel();
    for (const auto& w : m.warnings()) UNSCOPED_INFO(w);
    REQUIRE(m.warnings().empty());
    REQUIRE(m.gate("cx", q({0, 1}))->overRotationRad > 0.0);
    REQUIRE(contains(ids(m.channelsFor("cx", q({0, 1}))), id::OverRotation));
    // r = 8e-3: p = 1.0667e-2 exceeds neither p_relax = 8.845e-3 nor p_coherent = 2.133e-3 alone,
    // but their sum; the depolarizing part is clamped with the second warning form (spec 08 §4.1).
    Overrides lower;
    lower.replaceEdge["0-1"]["gate_error_2q"] = 8e-3;
    NOISE_REQUIRE_OK(m.setOverrides(lower));
    const GateNoise* cx = m.gate("cx", q({0, 1}));
    REQUIRE(cx->pRelaxation == Approx(8.844836071792532e-3).epsilon(1e-9)); // computed independently
    REQUIRE(cx->pRelaxation < cx->pTotal);
    REQUIRE(cx->clamped);
    REQUIRE(cx->depolarizing == 0.0);
    REQUIRE(m.warnings().size() == 1);
    REQUIRE(m.warnings()[0].find("relaxation plus coherent error exceed reported gate error for cx on 0-1") != std::string::npos);
    REQUIRE_FALSE(contains(ids(m.channelsFor("cx", q({0, 1}))), id::Depolarizing2q));

    Overrides o;
    o.scaleT1 = 0.5;
    o.scaleT2 = 1.5;                            // qubit 1: 75 µs > 2·30 µs → clamped with a warning
    o.replaceQubit[0]["t1_us"] = 10.0;          // absolute, wins over the scale
    o.replaceQubit[0]["t2_us"] = 15.0;
    o.replaceQubit[0]["readout_e01"] = 0.2;
    o.replaceQubit[0]["t1us"] = 3.0;            // typo: warned, ignored
    o.replaceEdge["1-0"]["zz_hz"] = 0.0;        // either edge orientation
    o.replaceEdge["0-1"]["gate_error_2q"] = 1e-2;
    o.disable = {std::string(id::Depolarizing1q), std::string(id::DetuningDrift), std::string(id::Readout)};
    NOISE_REQUIRE_OK(m.setOverrides(o));
    REQUIRE(m.overrides() == o);
    REQUIRE(m.qubit(QubitIndex{0})->t1S == Approx(10e-6).epsilon(1e-15));
    REQUIRE(m.qubit(QubitIndex{1})->t1S == Approx(30e-6).epsilon(1e-15));
    REQUIRE(m.qubit(QubitIndex{1})->t2S == Approx(60e-6).epsilon(1e-15)); // clamped to 2 T1
    REQUIRE(m.qubit(QubitIndex{0})->readoutAssignment[0][1] == 0.2);
    REQUIRE(m.gate("cx", q({1, 0}))->errorR == 1e-2);
    REQUIRE(m.edge(0, 1)->zzHz == 0.0);
    REQUIRE(m.crosstalkChannels(1e-6).empty());
    const auto has = [&](std::string_view text) {
        return std::any_of(m.warnings().begin(), m.warnings().end(), [&](const std::string& w) { return w.find(text) != std::string::npos; });
    };
    REQUIRE(has("exceeds 2 T1 = 60 us on qubit 1"));
    REQUIRE(has("overrides.replace.qubits.0.t1us is not a known field"));
    const auto sx = ids(m.channelsFor("sx", q({0})));
    REQUIRE(contains(sx, id::ThermalRelaxation));
    REQUIRE_FALSE(contains(sx, id::Depolarizing1q));
    REQUIRE_FALSE(contains(sx, id::DetuningDrift));
    REQUIRE(m.idleChannels(QubitIndex{0}, 1e-6).size() == 1);
    auto ro = m.readout(q({0, 1}));
    NOISE_REQUIRE_OK(ro);
    REQUIRE(ro->isIdeal());

    // A rejected override leaves the model exactly as it was.
    const core::Json before = m.toJson();
    const double t1Before = m.qubit(QubitIndex{0})->t1S;
    Overrides bad = o;
    bad.scaleT1 = 0.0;
    auto refused = m.setOverrides(bad);
    REQUIRE_FALSE(refused);
    REQUIRE(refused.error().code == err::InvalidParameter);
    Overrides badValue = o;
    badValue.replaceQubit[1]["p_thermal"] = 0.7;
    REQUIRE_FALSE(m.setOverrides(badValue));
    REQUIRE(m.toJson() == before);
    REQUIRE(m.qubit(QubitIndex{0})->t1S == t1Before);
    REQUIRE(m.overrides() == o);
}

TEST_CASE("the drive-line photon number sets a floor on the thermal population (spec 08 §2.2)") {
    NoiseModel m = labModel();
    const double photons[] = {0.25, 1e-4};
    NOISE_REQUIRE_OK(m.setLinePhotonNumbers(photons));
    const QubitNoise* q0 = m.qubit(QubitIndex{0});
    REQUIRE(q0->pThermalLine == Approx(0.25 / 1.5).epsilon(1e-15));
    REQUIRE(q0->pThermalCalibration == 0.01);
    REQUIRE(q0->pThermal == q0->pThermalLine); // the warm line wins
    const QubitNoise* q1 = m.qubit(QubitIndex{1});
    REQUIRE(q1->pThermal == 0.002); // the calibration wins
    const auto prep = m.preparationChannels(q({0}));
    REQUIRE(prep.size() == 1);
    auto k = prep[0].channel->kraus(prep[0].context);
    NOISE_REQUIRE_OK(k);
    REQUIRE(k->pauliWeights.back() == Approx(0.25 / 1.5).epsilon(1e-15));
    const double tooMany[] = {0.1, 0.1, 0.1};
    REQUIRE_FALSE(m.setLinePhotonNumbers(tooMany));
    const double negative[] = {-1.0};
    REQUIRE_FALSE(m.setLinePhotonNumbers(negative));
    REQUIRE(m.qubit(QubitIndex{0})->pThermal == Approx(0.25 / 1.5).epsilon(1e-15)); // unchanged by the refusals
}

TEST_CASE("reading a qubit dephases its unmeasured feedline neighbours (spec 08 §3)") {
    const NoiseModel m = labModel();
    const auto chans = m.measurementChannels(q({0}));
    REQUIRE(chans.size() == 1);
    REQUIRE(chans[0].channel->id() == id::MeasurementDephasing);
    REQUIRE(chans[0].qubits == q({1}));
    REQUIRE(chans[0].placement == Placement::During);
    REQUIRE(chans[0].context.durationS == Approx(500e-9).epsilon(1e-15)); // the measured qubit's pulse
    core::Random rng(1);
    qsim::DensityMatrixBackend dm;
    NOISE_REQUIRE_OK(dm.allocate(2));
    NOISE_REQUIRE_OK(dm.applyGate(H(), q({1})));
    apply(dm, chans[0], rng);
    const double lambda = 0.5 * (1.0 - std::exp(-2.0 * 500e-9 / 50e-6));
    REQUIRE(expectation(dm, "XI") == Approx(std::sqrt(1.0 - lambda)).margin(1e-15));
    REQUIRE(m.measurementChannels(q({1})).empty()); // qubit 0 has no crosstalk dephasing scale
    REQUIRE(m.measurementChannels(q({0, 1})).empty());
}

TEST_CASE("isPauliOnly tells whether the stabilizer backend can run the model without a twirl") {
    NoiseModel m = labModel();
    REQUIRE_FALSE(m.isPauliOnly()); // amplitude damping, over-rotation and ZZ are present
    Overrides o;
    o.disable = {std::string(id::ThermalRelaxation), std::string(id::OverRotation), std::string(id::ZzCrosstalk)};
    NOISE_REQUIRE_OK(m.setOverrides(o));
    REQUIRE(m.isPauliOnly()); // depolarizing, drift (averaged), reset and preparation are Pauli channels
    auto pure = NoiseModel::fromJson(core::Json::parse(R"({"qubits": {"0": {"t2_us": 40}}, "gates": {"x": {"0": {"error": 1e-3}}}})"));
    NOISE_REQUIRE_OK(pure);
    REQUIRE(pure->isPauliOnly()); // T1 absent: thermal_relaxation is pure dephasing
    REQUIRE(ids(pure->idleChannels(QubitIndex{0}, 1e-6)) == std::vector<std::string_view>{id::ThermalRelaxation});
}
