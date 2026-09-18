// Spec 08 §5, spec 23 §1, spec 04 §8 — `qlab.noise/1`: lossless round trip, the spec example, unknown
// fields written back, and loader errors that name the field path.
#include "Hardware/Hardware.hpp"
#include "NoiseTestSupport.hpp"
#include <catch2/catch_approx.hpp>
#include <filesystem>

using namespace ntest;
using namespace qlab::noise;
using Catch::Approx;

namespace {
void requireSameChannels(const std::vector<AttachedChannel>& a, const std::vector<AttachedChannel>& b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].channel->id() == b[i].channel->id());
        REQUIRE(a[i].placement == b[i].placement);
        REQUIRE(a[i].qubits == b[i].qubits);
        REQUIRE(a[i].context.durationS == b[i].context.durationS);
        auto ka = a[i].channel->kraus(a[i].context);
        auto kb = b[i].channel->kraus(b[i].context);
        NOISE_REQUIRE_OK(ka);
        NOISE_REQUIRE_OK(kb);
        REQUIRE(ka->ops.size() == kb->ops.size());
        for (std::size_t k = 0; k < ka->ops.size(); ++k) REQUIRE(ka->ops[k].data == kb->ops[k].data); // bit-exact
    }
}
// Everything a consumer can observe, compared exactly.
void requireIdentical(const NoiseModel& a, const NoiseModel& b) {
    REQUIRE(a.toJson() == b.toJson());
    REQUIRE(a.levels() == b.levels());
    REQUIRE(a.overrides() == b.overrides());
    REQUIRE(std::vector<std::string>(a.warnings().begin(), a.warnings().end()) ==
            std::vector<std::string>(b.warnings().begin(), b.warnings().end()));
    REQUIRE(a.qubitCount() == b.qubitCount());
    for (std::uint32_t qb = 0; qb < a.qubitCount(); ++qb) {
        const QubitNoise& x = a.qubits()[qb];
        const QubitNoise& y = b.qubits()[qb];
        REQUIRE((x.t1S == y.t1S && x.t2S == y.t2S && x.t2StarS == y.t2StarS && x.frequencyHz == y.frequencyHz));
        REQUIRE((x.pThermal == y.pThermal && x.pThermalLine == y.pThermalLine && x.resetError == y.resetError));
        REQUIRE((x.readoutAssignment == y.readoutAssignment && x.readoutDurationS == y.readoutDurationS));
        REQUIRE((x.readoutDephasing == y.readoutDephasing && x.driftSigmaHz == y.driftSigmaHz));
        requireSameChannels(a.idleChannels(QubitIndex{qb}, 3e-6), b.idleChannels(QubitIndex{qb}, 3e-6));
    }
    const auto ga = a.gates(), gb = b.gates();
    REQUIRE(ga.size() == gb.size());
    for (std::size_t i = 0; i < ga.size(); ++i) {
        REQUIRE((ga[i]->gate == gb[i]->gate && ga[i]->qubits == gb[i]->qubits && ga[i]->errorR == gb[i]->errorR));
        REQUIRE((ga[i]->pRelaxation == gb[i]->pRelaxation && ga[i]->depolarizing == gb[i]->depolarizing));
        REQUIRE((ga[i]->overRotationRad == gb[i]->overRotationRad && ga[i]->clamped == gb[i]->clamped));
        std::vector<QubitIndex> targets;
        for (auto t : ga[i]->qubits) targets.push_back(QubitIndex{t});
        requireSameChannels(a.channelsFor(ga[i]->gate, targets), b.channelsFor(gb[i]->gate, targets));
    }
}
} // namespace

TEST_CASE("the JSON round trip is lossless for calibrated models") {
    for (const char* deviceId : {"sc_fixed_5", "sc_heavyhex_27", "sc_tunable_grid_54"}) {
        INFO(deviceId);
        auto dev = hw::loadShippedDevice(deviceId);
        NOISE_REQUIRE_OK(dev);
        NoiseOptions options;
        options.levels = 3;
        options.linePhotonNumbers = {0.004, 0.0, 0.2};
        options.overrides.scaleT1 = 0.9;
        options.overrides.replaceQubit[1]["t1_us"] = 55.5;
        options.overrides.replaceEdge["1-0"]["zz_hz"] = 1234.5;
        options.overrides.setEnabled(id::MeasurementDephasing, false);
        auto m = NoiseModel::fromCalibration(dev->device, dev->calibration, options);
        NOISE_REQUIRE_OK(m);
        const std::string text = m->serialize();
        auto back = NoiseModel::parse(text);
        NOISE_REQUIRE_OK(back);
        requireIdentical(*m, *back);
        auto again = NoiseModel::parse(back->serialize());
        NOISE_REQUIRE_OK(again);
        REQUIRE(again->toJson() == m->toJson());
    }
    // Couplers have no lifetimes: omitted in JSON (no infinities, spec 23 §1) and still absent on reload.
    auto grid = hw::loadShippedDevice("sc_tunable_grid_54");
    NOISE_REQUIRE_OK(grid);
    auto model = NoiseModel::fromCalibration(grid->device, grid->calibration);
    NOISE_REQUIRE_OK(model);
    std::uint32_t coupler = 0;
    while (!grid->device.isCoupler(coupler)) ++coupler;
    REQUIRE_FALSE(model->toJson()["qubits"][std::to_string(coupler)].contains("t1_us"));
    const auto file = std::filesystem::temp_directory_path() / "qxl_noise_model_roundtrip.json";
    NOISE_REQUIRE_OK(model->save(file));
    auto loaded = NoiseModel::load(file);
    std::filesystem::remove(file);
    NOISE_REQUIRE_OK(loaded);
    requireIdentical(*model, *loaded);
    REQUIRE(std::isinf(loaded->qubit(QubitIndex{coupler})->t1S));
}

TEST_CASE("the spec 08 §5 example document loads with the §4.1 defaults") {
    auto m = NoiseModel::fromJson(core::Json::parse(R"({
      "schema": "qlab.noise/1",
      "source": { "device": "sc_heavyhex_27", "calibration": "2025-03-14T09:00:00Z" },
      "levels": 2,
      "qubits": {
        "0": { "t1_us": 182.4, "t2_us": 141.0, "t2_star_us": 60.2, "frequency_ghz": 4.9112,
               "readout": { "assignment": [[0.985, 0.015], [0.031, 0.969]] },
               "reset_error": 0.004, "p_thermal": 0.0012 }
      },
      "gates": {
        "sx": { "0": { "error": 2.1e-4, "duration_ns": 32, "coherent_fraction": 0.1 } },
        "cx": { "0-1": { "error": 6.8e-3, "duration_ns": 380, "leakage": 1.5e-4 } }
      },
      "edges": { "0-1": { "zz_hz": 42e3 } },
      "readout_groups": [ { "qubits": [0,1,2,3], "assignment": null } ],
      "overrides": { "scale_t1": 1.0, "scale_t2": 1.0, "scale_gate_error": 1.0, "disable": [] }
    })"));
    NOISE_REQUIRE_OK(m);
    REQUIRE(m->deviceId() == "sc_heavyhex_27");
    REQUIRE(m->calibrationStamp() == "2025-03-14T09:00:00Z");
    REQUIRE(m->qubitCount() == 4); // qubits referenced by the gate and the readout group
    const QubitNoise* q0 = m->qubit(QubitIndex{0});
    REQUIRE(q0->t1S == Approx(182.4e-6).epsilon(1e-15));
    REQUIRE(q0->frequencyHz == Approx(4.9112e9).epsilon(1e-15));
    REQUIRE(q0->readoutAssignment[1][0] == 0.031);
    REQUIRE(q0->driftSigmaHz == Approx(driftSigmaFromT2Star(60.2e-6)).epsilon(1e-15));
    REQUIRE(q0->readoutDurationS == 0.0);
    REQUIRE(std::isinf(m->qubit(QubitIndex{1})->t1S)); // implicit slots are noiseless
    REQUIRE(m->qubit(QubitIndex{1})->resetError == 0.0);
    const GateNoise* sx = m->gate("sx", q({0}));
    REQUIRE(sx->pCoherent == Approx(2.0 * 0.1 * 2.1e-4).epsilon(1e-12));
    auto rotation = channels::overRotation("X", sx->overRotationRad);
    NOISE_REQUIRE_OK(rotation);
    REQUIRE(1.0 - averageGateFidelity(*rotation) == Approx(0.1 * 2.1e-4).epsilon(1e-9)); // spec 08 §4.1
    REQUIRE(sx->depolarizing == Approx(sx->pTotal - sx->pRelaxation - sx->pCoherent).epsilon(1e-15));
    const GateNoise* cx = m->gate("cx", q({1, 0}));
    REQUIRE(cx->leakage == 1.5e-4);
    REQUIRE(cx->seepage == 1.5e-4);
    REQUIRE(m->edge(1, 0)->zzHz == 42e3);
    auto ro = m->readout(q({0, 1, 2, 3}));
    NOISE_REQUIRE_OK(ro);
    REQUIRE(ro->factors().size() == 4); // null group assignment: tensor product of the single-qubit matrices
    auto back = NoiseModel::fromJson(m->toJson());
    NOISE_REQUIRE_OK(back);
    requireIdentical(*m, *back);
}

TEST_CASE("unknown fields are written back and loader errors name the field path") {
    const core::Json doc = core::Json::parse(R"({
      "schema": "qlab.noise/1", "comment": "hand-tuned",
      "source": {"device": "lab", "operator": "ET"},
      "qubits": {"0": {"t1_us": 80, "note": "TLS at 4.91 GHz", "readout": {"assignment": [[0.9, 0.1], [0.2, 0.8]], "iq": [1, 2]}}},
      "gates": {"x": {"0": {"error": 1e-3, "duration_ns": 40, "pulse": "drag"}}},
      "edges": {"0-1": {"zz_hz": 1e3, "coupler_hz": 5}},
      "readout_groups": [{"qubits": [0, 1], "assignment": null, "feedline": 2}],
      "overrides": {"scale_t1": 1, "slider": 0.3, "replace": {"gates": {"x": 1}, "qubits": {"0": {"t2_us": 90}}}}
    })");
    auto m = NoiseModel::fromJson(doc);
    NOISE_REQUIRE_OK(m);
    const core::Json out = m->toJson();
    REQUIRE(out["comment"] == "hand-tuned");
    REQUIRE(out["source"]["operator"] == "ET");
    REQUIRE(out["qubits"]["0"]["note"] == "TLS at 4.91 GHz");
    REQUIRE(out["qubits"]["0"]["readout"]["iq"] == core::Json::array({1, 2}));
    REQUIRE(out["gates"]["x"]["0"]["pulse"] == "drag");
    REQUIRE(out["edges"]["0-1"]["coupler_hz"] == 5);
    REQUIRE(out["readout_groups"][0]["feedline"] == 2);
    REQUIRE(out["overrides"]["slider"] == 0.3);
    REQUIRE(out["overrides"]["replace"]["gates"]["x"] == 1);
    REQUIRE(m->qubit(QubitIndex{0})->t2S == Approx(90e-6).epsilon(1e-15));

    auto error = [](std::string_view text) {
        auto r = NoiseModel::fromJson(core::Json::parse(text));
        REQUIRE_FALSE(r);
        return r.error();
    };
    auto e1 = error(R"({"qubits": {"0": {"t1_us": "long"}}})");
    REQUIRE(e1.code == err::BadJson);
    REQUIRE(e1.message.find("noise.qubits.0.t1_us") != std::string::npos);
    REQUIRE(error(R"({"qubits": {"zero": {}}})").message.find("'zero'") != std::string::npos);
    REQUIRE(error(R"({"gates": {"cx": {"0-x": {}}}})").message.find("noise.gates.cx.0-x") != std::string::npos);
    REQUIRE(error(R"({"gates": {"cx": {"0-1-2": {}}}})").code == err::BadJson);
    REQUIRE(error(R"({"readout_groups": [{"qubits": [0, 1], "assignment": [[1, 0], [0, 1]]}]})").code == err::BadReadout);
    auto e2 = error(R"({"qubits": {"0": {"readout": {"assignment": [[0.9, 0.2], [0.1, 0.8]]}}}})");
    REQUIRE(e2.code == err::BadReadout);
    REQUIRE(e2.message.find("qubits.0.readout.assignment") != std::string::npos);
    REQUIRE(error(R"({"levels": 4})").code == err::LevelsMismatch);
    REQUIRE(error(R"({"schema": "qlab.noise/2"})").code == err::BadJson);
    auto e3 = error(R"({"qubits": {"0": {"t1_us": -5}}})");
    REQUIRE(e3.code == err::InvalidParameter);
    REQUIRE(e3.message.find("qubits.0.t1_us") != std::string::npos);
    REQUIRE(error(R"([1, 2])").code == err::BadJson);
    REQUIRE(error(R"({"overrides": {"disable": [3]}})").message.find("noise.overrides.disable[0]") != std::string::npos);
    REQUIRE_FALSE(NoiseModel::parse("{ not json"));
    REQUIRE_FALSE(NoiseModel::parse(core::JsonEnvelope::serialize("calibration", core::Json::object())));
}
