// Spec 15 §6–§9, T12 §1–§4, §7, §9 — the hardware estimators: the worked wall-time examples of
// §6, the product fidelity of §7 (i), the resource and classical-cost rows of §8, and the §9
// record.
#include "Data/Fidelity.hpp"
#include "RuntimeTestUtil.hpp"
#include <cmath>

using namespace rtest;
using Catch::Approx;

namespace {
// The shipped calibration files are generated with per-qubit spreads around the nominals of spec
// 09 §4; the worked examples of §6/§7 quote the nominals themselves. This is that calibration.
hw::Calibration nominal(const hw::Calibration& base, double t1S, double t2S, double e1, double e2,
                        double eRo, double d1S, double roS) {
    hw::Calibration c = base;
    for (hw::QubitCal& q : c.qubits) {
        q.t1.value = units::Time{t1S};
        q.t1.sigma = units::Time{0.0};
        q.t2echo.value = units::Time{t2S};
        q.t2star.value = units::Time{std::min(t2S, 0.5 * t2S)};
        q.gateError1q.value = e1;
        q.gateError1q.sigma = 0.0;
        q.duration1q.value = units::Time{d1S};
        q.readoutDuration.value = units::Time{roS};
        q.readoutAssignment = {{{1.0 - eRo, eRo}, {eRo, 1.0 - eRo}}};
    }
    for (auto& [key, e] : c.edges) {
        e.gateError2q.value = e2;
        e.gateError2q.sigma = 0.0;
    }
    return c;
}

struct Bell {
    Lab* lab = nullptr;
    Lab::Job job;
    const compiler::CompiledProgram* program = nullptr;
    hw::Calibration cal;

    EstimateInput input(std::uint64_t shots = 1024) const {
        EstimateInput in;
        in.device = lab->session.device();
        in.calibration = &cal;
        in.program = program;
        in.shots = shots;
        in.usedQubits = usedQubits(program->circuit);
        in.measuredQubits = in.usedQubits;
        return in;
    }
};

Bell bellOn(const std::string& deviceId, bool useNominal) {
    Bell b;
    b.lab = &lab(deviceId);
    b.job = b.lab->compile(readAsset("Programs/Examples/Basics/bell.qasm"));
    b.program = b.lab->session.compiled(b.job.handle);
    REQUIRE(b.program != nullptr);
    const hw::Calibration& shipped = *b.lab->session.calibration();
    // Spec 09 §4 nominals: transmon 32 ns / 380 ns gates, 640 ns readout, T1 150 µs, T2 95 µs,
    // ε1 3.5e-4, ε2 7.5e-3, F_ro 0.98; ion 10 µs / 200 µs, 300 µs readout, ε1 1e-4, ε2 4e-3, F_ro
    // 0.997.
    if (!useNominal)
        b.cal = shipped;
    else if (deviceId == "ion_chain_11")
        b.cal = nominal(shipped, 1e9, 1.0, 1e-4, 4e-3, 0.003, 10e-6, 300e-6);
    else
        b.cal = nominal(shipped, 150e-6, 95e-6, 3.5e-4, 7.5e-3, 0.02, 32e-9, 640e-9);
    return b;
}
} // namespace

TEST_CASE("spec 15 section 6: Bell x1024 on sc_heavyhex_27 with active and with passive reset") {
    const Bell b = bellOn("sc_heavyhex_27", true);
    EstimateInput in = b.input(1024);
    auto active = estimateWallTime(in);
    REQUIRE(active);
    // T_circ = sx 32 ns + cx 380 ns (quantised to the 3.552 ns granule of the device).
    REQUIRE(active->circuitS == Approx(412e-9).margin(0.1e-9));
    REQUIRE(active->readoutS == Approx(800e-9).margin(1e-12)); // 640 ns + 160 ns ring-down
    REQUIRE(active->resetS == Approx(872e-9).margin(1e-12));   // 640 + 200 + 32 ns (T12 §1.3)
    REQUIRE(active->gapS == Approx(1.0e-6).margin(1e-12));
    REQUIRE(active->loadS == Approx(50e-3).margin(1e-12));
    REQUIRE(active->perShotS == Approx(3.084e-6).margin(1e-9));
    REQUIRE(active->shotsS == Approx(3.158e-3).margin(2e-6));
    REQUIRE(active->feedbackTotalS == 0.0);                 // no Branch in the Bell program
    REQUIRE(active->valueS == Approx(0.0532).margin(5e-5)); // spec 15 §6: 53.2 ms

    in.resetPolicy = hw::ResetPolicy::Passive;
    auto passive = estimateWallTime(in);
    REQUIRE(passive);
    REQUIRE(passive->resetS == Approx(750e-6).margin(1e-12));    // max(5 × 150 µs, 250 µs)
    REQUIRE(passive->perShotS == Approx(752.2e-6).margin(5e-8)); // spec 15 §6 quotes 752.2 µs
    REQUIRE(passive->shotsS == Approx(0.770).margin(5e-4));
    REQUIRE(passive->valueS == Approx(0.820).margin(5e-4)); // spec 15 §6: 0.820 s
}

TEST_CASE("spec 15 section 6: Bell x1024 on ion_chain_11") {
    const Bell b = bellOn("ion_chain_11", true);
    auto w = estimateWallTime(b.input(1024));
    REQUIRE(w);
    REQUIRE(w->resetS == Approx(1.5e-3).margin(1e-12));   // cooling
    REQUIRE(w->readoutS == Approx(300e-6).margin(1e-12)); // fluorescence, no ring-down
    REQUIRE(w->gapS == Approx(100e-6).margin(1e-12));
    REQUIRE(w->loadS == Approx(100e-3).margin(1e-12));
    // Compiled: rx(pi) on one ion, ms(pi/2), then the two dressing gates on DIFFERENT ions, which
    // the ASAP schedule runs in parallel: 10 + 200 + 10 = 220 µs, not the 230 µs of a serial
    // dressing pair.
    REQUIRE(w->circuitS == Approx(220e-6).margin(0.5e-6));
    REQUIRE(w->perShotS == Approx(2120e-6).margin(0.5e-6));
    REQUIRE(w->valueS == Approx(2.27).margin(5e-3));
}

TEST_CASE("spec 15 section 7 (i): the product fidelity of the Bell example") {
    const Bell sc = bellOn("sc_heavyhex_27", true);
    auto f = estimateFidelityFast(sc.input());
    REQUIRE(f);
    REQUIRE(f->gateProduct == Approx(0.99965 * 0.9925).margin(1e-6)); // one sx, one cx; rz is free
    REQUIRE(f->readoutProduct == Approx(0.98 * 0.98).margin(1e-9));
    REQUIRE(f->idleProduct <= 1.0);
    REQUIRE(f->fast == Approx(0.953).margin(5e-4)); // spec 15 §7
    REQUIRE(f->cls == data::FidelityClass::Model);
    REQUIRE(f->caveats.size() == 4);
    REQUIRE(f->low <= f->fast);
    REQUIRE(f->high >= f->fast);

    const Bell ion = bellOn("ion_chain_11", true);
    auto g = estimateFidelityFast(ion.input());
    REQUIRE(g);
    REQUIRE(g->gateProduct == Approx(0.9997 * 0.996).margin(1e-5)); // three 1q pulses, one ms
    REQUIRE(g->readoutProduct == Approx(0.997 * 0.997).margin(1e-9));
    REQUIRE(g->fast == Approx(0.990).margin(5e-4)); // spec 15 §7
}

TEST_CASE("the fidelity estimate widens with the calibration sigmas and shrinks with more gates") {
    const Bell shipped = bellOn("sc_heavyhex_27", false);
    auto f = estimateFidelityFast(shipped.input());
    REQUIRE(f);
    // The shipped file carries a 1σ on every RB error, so the band is not degenerate (T12 §7).
    REQUIRE(f->low < f->fast);
    REQUIRE(f->high > f->fast);
    REQUIRE(f->sigmaLog > 0.0);
    // Twenty entangling gates cost roughly twenty times the error of one. The `t` between them
    // keeps the optimizer from cancelling the CX pairs (it is a virtual Z, so it costs no time and
    // no error).
    Lab& l = lab("sc_heavyhex_27");
    auto job =
        l.compile(source("qubit[2] q;\nbit[2] c;\nh q[0];\n"
                         "for int i in [0:19] { cx q[0], q[1]; t q[1]; }\nc = measure q;\n"));
    EstimateInput deep = shipped.input();
    deep.program = l.session.compiled(job.handle);
    deep.calibration = l.session.calibration();
    deep.usedQubits = usedQubits(deep.program->circuit);
    deep.measuredQubits = deep.usedQubits;
    auto d = estimateFidelityFast(deep);
    REQUIRE(d);
    REQUIRE(d->fast < f->fast);
    REQUIRE(d->gateProduct < 0.9);
}

TEST_CASE("resources and classical cost follow the metrics and this host") {
    const Bell b = bellOn("sc_heavyhex_27", false);
    const ResourceSummary r = summarizeResources(b.input());
    REQUIRE(r.qubits == 2);
    REQUIRE(r.twoQubit == 1);
    REQUIRE(r.swaps == 0);
    REQUIRE(r.classicalBits == 2);
    REQUIRE(r.tCount == 0);
    REQUIRE(r.branches == 0);
    REQUIRE(r.circuitTimeS == Approx(412e-9).margin(0.1e-9));

    // Spec 15 §8: the T count is read off the PRE-decomposition circuit, so `t` survives the
    // rewrite into rz, and an arbitrary rz angle is a non-Clifford rotation instead.
    Lab& l = lab("sc_heavyhex_27");
    auto job =
        l.compile(source("qubit[2] q;\nbit[2] c;\nh q[0];\nt q[0];\ntdg q[1];\nrz(0.37) q[1];\n"
                         "cx q[0], q[1];\nc = measure q;\n"));
    EstimateInput in = b.input();
    in.program = l.session.compiled(job.handle);
    in.usedQubits = usedQubits(in.program->circuit);
    const ResourceSummary t = summarizeResources(in);
    REQUIRE(t.tCount == 2);
    REQUIRE(t.rotations == 1);

    const ClassicalCost c = classicalCost(27, 4000, 43.9e-6, 1024);
    REQUIRE(c.stateVectorBytes == Approx(16.0 * std::pow(2.0, 27)).margin(1.0)); // 2 GiB (T12 §2.4)
    REQUIRE(c.densityMatrixBytes == Approx(16.0 * std::pow(4.0, 27)).epsilon(1e-12));
    REQUIRE(c.gateCostS > 0.0);
    REQUIRE(c.estimatedTimeS == Approx(4000.0 * std::pow(2.0, 27) * c.gateCostS).epsilon(1e-9));
    REQUIRE(c.hostMaxQubits >= 20);
    REQUIRE(c.crossoverQubits > 0.0);
}

TEST_CASE("the estimate record matches the spec 15 section 9 schema and carries its assumptions") {
    const Bell b = bellOn("sc_heavyhex_27", true);
    auto e = estimate(b.input(1024));
    REQUIRE(e);
    REQUIRE(e->cls == data::FidelityClass::Model);
    const core::Json j = e->toJson();
    for (const char* key : {"device", "calibration_timestamp", "class", "wall_time", "fidelity",
                            "resources", "classical_cost", "qec", "assumptions", "comparison"})
        REQUIRE(j.contains(key));
    REQUIRE(j["device"] == "sc_heavyhex_27");
    REQUIRE(j["class"] == "Model");
    REQUIRE(j["wall_time"]["value_s"].get<double>() == Approx(0.0532).margin(5e-5));
    for (const char* key :
         {"load", "per_shot_s", "shots", "reset", "circuit", "readout", "gap", "feedback_total"})
        REQUIRE(j["wall_time"]["terms"].contains(key));
    REQUIRE(j["wall_time"]["terms"]["shots"].get<std::uint64_t>() == 1024);
    REQUIRE(j["fidelity"]["fast"].get<double>() == Approx(0.953).margin(5e-4));
    REQUIRE(j["fidelity"]["simulated"].is_null());
    REQUIRE(j["resources"]["two_qubit"].get<int>() == 1);
    REQUIRE(j["classical_cost"]["statevector_bytes"].get<double>() == Approx(64.0).margin(1e-9));
    REQUIRE(j["qec"].is_null()); // the Bell fidelity is far above the 0.5 threshold
    // Every assumption key renders a sentence; the §9 list is present verbatim.
    const std::vector<std::string> keys = j["assumptions"].get<std::vector<std::string>>();
    for (const char* wanted : {"queue_time_excluded", "reset_policy:active", "independent_errors",
                               "average_gate_fidelities", "no_crosstalk", "calibration_static"})
        REQUIRE(std::find(keys.begin(), keys.end(), wanted) != keys.end());
    for (const std::string& k : keys)
        REQUIRE(!assumptionText(k).empty());
    REQUIRE(assumptionKeys().size() >= 8);
    REQUIRE(assumptionText("nonsense").empty());
    // The envelope is the one the report exports.
    const std::string text = e->serialize();
    auto env = core::JsonEnvelope::parse(text, "qlab.estimate");
    REQUIRE(env);
    REQUIRE(env->data["assumptions"].size() == keys.size());
}

TEST_CASE("the section 9 comparison holds one row per device that can accept the circuit") {
    Lab& l = lab("sc_fixed_5");
    RunOptions options;
    options.noise = NoiseSource::Ideal;
    options.shots = 1024;
    options.seed = 3;
    options.compareDevices = true;
    const RunResult r = l.run(readAsset("Programs/Examples/Basics/bell.qasm"), options);
    const std::vector<DeviceComparison>& rows = r.estimate.comparison;
    REQUIRE(rows.size() >= 3);
    for (const DeviceComparison& c : rows) {
        INFO(c.device << ": " << c.wallTimeS << " s, F = " << c.fidelityFast);
        REQUIRE(c.device != "sc_fixed_5"); // the run's own device is not repeated
        REQUIRE(c.wallTimeS > 0.0);
        REQUIRE(c.fidelityFast > 0.0);
        REQUIRE(c.fidelityFast <= 1.0);
    }
    // The ion chain is slower per shot and more accurate than the transmons (T12 §1.4).
    const auto ion = std::find_if(rows.begin(), rows.end(), [](const DeviceComparison& c) {
        return c.device == "ion_chain_11";
    });
    const auto hex = std::find_if(rows.begin(), rows.end(), [](const DeviceComparison& c) {
        return c.device == "sc_heavyhex_27";
    });
    REQUIRE(ion != rows.end());
    REQUIRE(hex != rows.end());
    REQUIRE(ion->wallTimeS > hex->wallTimeS);
    REQUIRE(ion->fidelityFast > hex->fidelityFast);
    const core::Json j = r.estimate.toJson();
    REQUIRE(j["comparison"].size() == rows.size());
    REQUIRE(j["comparison"][0].contains("wall_time_s"));
    REQUIRE(j["comparison"][0].contains("fidelity_fast"));
}

TEST_CASE("the QEC row appears once the uncorrected fidelity falls below the threshold") {
    Lab& l = lab("sc_heavyhex_27");
    auto job =
        l.compile(source("qubit[2] q;\nbit[2] c;\nh q[0];\nt q[0];\n"
                         "for int i in [0:149] { cx q[0], q[1]; t q[1]; }\nc = measure q;\n"));
    EstimateInput in;
    in.device = l.session.device();
    in.calibration = l.session.calibration();
    in.program = l.session.compiled(job.handle);
    REQUIRE(in.program != nullptr);
    in.shots = 1024;
    in.usedQubits = usedQubits(in.program->circuit);
    in.measuredQubits = in.usedQubits;
    auto e = estimate(in);
    REQUIRE(e);
    REQUIRE(e->fidelity.fast < 0.5);
    REQUIRE(e->qec.has_value());
    REQUIRE(e->qec->distance >= 3);
    REQUIRE(e->qec->distance % 2 == 1);
    REQUIRE(e->qec->physicalQubits > e->resources.qubits);
    REQUIRE(e->qec->wallTimeS > 0.0);
    REQUIRE(e->qec->cls == data::FidelityClass::Model);
    REQUIRE(!e->qec->assumptions.empty());
    const auto& keys = e->assumptions;
    REQUIRE(std::find(keys.begin(), keys.end(), "qec_surface_code") != keys.end());
    REQUIRE(!e->toJson()["qec"].is_null());
    // The estimate itself stays NISQ: the QEC row is separate, never blended (spec 15 §8).
    auto nisq = estimateWallTime(in);
    REQUIRE(nisq);
    REQUIRE(e->wallTime.valueS == Approx(nisq->valueS).epsilon(1e-15));
    REQUIRE(e->qec->syndromeCycles > 0.0);
}
