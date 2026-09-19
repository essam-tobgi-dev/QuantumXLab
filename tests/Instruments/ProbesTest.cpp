// Spec 12 §12; spec 00 §6 — the simulator-only probes. Oracles: a Bell state's reductions
// (Bloch vector, purity, entropy, concurrence, mutual information), the squared fidelity
// convention, process fidelity against an ideal unitary, the jump record, the thermal truth and
// the leakage population — and, on every one of them, the Simulator-only flag.
#include "InstrTestSupport.hpp"

#include <catch2/catch_approx.hpp>

#include <numbers>

using namespace qlab;
using namespace qlab::instr;
using Catch::Approx;
using instrtest::powerOn;

namespace {
const double kInvSqrt2 = 1.0 / std::numbers::sqrt2;

// |Φ+> = (|00> + |11>)/√2 as a state-vector snapshot (little-endian basis indices).
qsim::Snapshot bellState() {
    qsim::Snapshot s;
    s.kind = qsim::Kind::StateVector;
    s.nQubits = 2;
    s.gateIndex = 3;
    s.cls = FidelityClass::Exact;
    s.amplitudes = std::vector<Complex>{{kInvSqrt2, 0.0}, {}, {}, {kInvSqrt2, 0.0}};
    return s;
}

qsim::Snapshot productState() { // |00>
    qsim::Snapshot s = bellState();
    s.amplitudes = std::vector<Complex>{{1.0, 0.0}, {}, {}, {}};
    return s;
}

// A probe bound to `run` over the sc_fixed_5 environment, powered on.
template <class Probe> struct ProbeBench {
    Probe probe;
    std::shared_ptr<InputHub> hub;
    explicit ProbeBench(RunView run, Environment env = instrtest::deviceEnvironment())
        : hub(instrtest::makeHub(std::move(env), std::move(run))) {
        Bindings b;
        b.inputs = hub;
        probe.bind(b);
        powerOn(probe);
    }
};

RunView bellRun() {
    RunView run;
    run.nQubits = 2;
    run.state = std::make_shared<const qsim::Snapshot>(bellState());
    return run;
}
} // namespace

TEST_CASE("probe_state: amplitudes, Bloch vectors and purities of a Bell state") {
    ProbeBench<StateProbe> bench(bellRun());
    auto amps = acquire(bench.probe, "amplitudes");
    REQUIRE(amps);
    REQUIRE(amps->simulatorOnly); // spec 00 §6
    REQUIRE(amps->cls == FidelityClass::Exact);
    REQUIRE(amps->size() == 4);
    REQUIRE(amps->y[0] == Approx(kInvSqrt2).margin(1e-15));
    REQUIRE(amps->y[3] == Approx(kInvSqrt2).margin(1e-15));
    REQUIRE(amps->y[1] == 0.0);
    REQUIRE(amps->y_im[3] == 0.0);
    REQUIRE(amps->marker("gate_index")->value == 3.0);

    auto probs = acquire(bench.probe, "probabilities");
    REQUIRE(probs);
    REQUIRE(probs->y[0] == Approx(0.5).margin(1e-15));
    REQUIRE(probs->y[3] == Approx(0.5).margin(1e-15));

    // Each half of |Φ+> is maximally mixed: r = 0, Tr ρ² = 1/2, while the pair stays pure.
    for (const char* ch : {"bloch_x", "bloch_y", "bloch_z"}) {
        auto t = acquire(bench.probe, ch);
        REQUIRE(t);
        REQUIRE(t->size() == 2);
        for (double y : t->y)
            REQUIRE(y == Approx(0.0).margin(1e-14));
    }
    auto purity = acquire(bench.probe, "purity");
    REQUIRE(purity);
    for (double y : purity->y)
        REQUIRE(y == Approx(0.5).epsilon(1e-12));
    REQUIRE(purity->marker("global")->value == Approx(1.0).epsilon(1e-12));
    REQUIRE(*bench.probe.query("purity") == Approx(1.0).epsilon(1e-12));
    REQUIRE(*bench.probe.query("qubit[1].purity") == Approx(0.5).epsilon(1e-12));
    REQUIRE(*bench.probe.query("qubit[0].bloch[2]") == Approx(0.0).margin(1e-14));

    // ρ_0 = I/2 (x = row·d + col).
    REQUIRE(bench.probe.set("qubits", std::string("0")));
    auto rho = acquire(bench.probe, "reduced");
    REQUIRE(rho);
    REQUIRE(rho->marker("dimension")->value == 2.0);
    REQUIRE(rho->y[0] == Approx(0.5).epsilon(1e-12));
    REQUIRE(rho->y[1] == Approx(0.0).margin(1e-14));
    REQUIRE(rho->y[3] == Approx(0.5).epsilon(1e-12));
    REQUIRE(rho->marker("purity")->value == Approx(0.5).epsilon(1e-12));
    // `qubits` is free text: a selection outside the register is reported by the acquisition.
    REQUIRE(bench.probe.set("qubits", std::string("7")));
    REQUIRE(acquire(bench.probe, "reduced").error().code == err::BadInput);

    // A ground-state product: r_z = +1 on both qubits (|0> is the north pole, DEVELOPMENT.md).
    RunView product = bellRun();
    product.state = std::make_shared<const qsim::Snapshot>(productState());
    bench.hub->publish(product);
    REQUIRE(bench.probe.set("qubits", std::string("")));
    auto z = acquire(bench.probe, "bloch_z");
    REQUIRE(z);
    for (double y : z->y)
        REQUIRE(y == Approx(1.0).epsilon(1e-12));
}

TEST_CASE("probe_entanglement: S = 1 bit, concurrence 1 and I(0:1) = 2 bits for a Bell pair") {
    ProbeBench<EntanglementProbe> bench(bellRun());
    auto entropy = acquire(bench.probe, "entropy");
    REQUIRE(entropy);
    REQUIRE(entropy->simulatorOnly);
    REQUIRE(entropy->size() == 2);
    for (double y : entropy->y)
        REQUIRE(y == Approx(1.0).epsilon(1e-10)); // −2·½·log₂½ = 1 bit

    auto concurrence = acquire(bench.probe, "concurrence");
    REQUIRE(concurrence);
    REQUIRE(concurrence->size() == 1);
    REQUIRE(concurrence->y[0] == Approx(1.0).epsilon(1e-9));
    REQUIRE(concurrence->aux.at("i")[0] == 0.0);
    REQUIRE(concurrence->aux.at("j")[0] == 1.0);

    auto mutual = acquire(bench.probe, "mutual_information");
    REQUIRE(mutual);
    REQUIRE(mutual->y[0] == Approx(2.0).epsilon(1e-10)); // S_0 + S_1 − S_01 = 1 + 1 − 0

    // S(A) = 0 for the whole register, 1 bit for either half.
    REQUIRE(bench.probe.set("partitions", std::string("0;1;0,1")));
    auto bipartition = acquire(bench.probe, "bipartition");
    REQUIRE(bipartition);
    REQUIRE(bipartition->size() == 3);
    REQUIRE(bipartition->y[0] == Approx(1.0).epsilon(1e-10));
    REQUIRE(bipartition->y[1] == Approx(1.0).epsilon(1e-10));
    REQUIRE(bipartition->y[2] == Approx(0.0).margin(1e-10));
    REQUIRE(bipartition->marker("S(0,1)") != nullptr);

    // A product state has no entanglement at all.
    RunView product = bellRun();
    product.state = std::make_shared<const qsim::Snapshot>(productState());
    bench.hub->publish(product);
    REQUIRE(acquire(bench.probe, "entropy")->y[0] == Approx(0.0).margin(1e-12));
    REQUIRE(acquire(bench.probe, "concurrence")->y[0] == Approx(0.0).margin(1e-9));
}

TEST_CASE("probe_fidelity: squared state fidelity per gate and process fidelity of a gate") {
    RunView run = bellRun();
    run.ideal = std::make_shared<const qsim::Snapshot>(bellState());
    ProbeBench<FidelityProbe> bench(run);
    auto exact = acquire(bench.probe, "state_fidelity");
    REQUIRE(exact);
    REQUIRE(exact->simulatorOnly);
    REQUIRE(exact->y.back() == Approx(1.0).epsilon(1e-12));
    REQUIRE(exact->x.back() == 3.0); // the gate index of the snapshot

    // |<Φ+|00>|² = ½ — the squared convention (DEVELOPMENT.md).
    RunView degraded = run;
    degraded.state = std::make_shared<const qsim::Snapshot>(productState());
    auto other = productState();
    other.gateIndex = 4;
    degraded.state = std::make_shared<const qsim::Snapshot>(other);
    bench.hub->publish(degraded);
    auto half = acquire(bench.probe, "state_fidelity");
    REQUIRE(half);
    REQUIRE(half->y.back() == Approx(0.5).epsilon(1e-12));
    REQUIRE(half->size() == 2); // the history keeps one point per gate index

    // Process fidelity of a noisy X: K = {√(1−p) X, √p Y} gives F_pro = 1 − p exactly.
    const double p = 0.1;
    GateComparison gate;
    gate.name = "x q0";
    gate.ideal = num::Matrix::fromRows({{0.0, 1.0}, {1.0, 0.0}});
    num::Matrix k0 = gate.ideal,
                k1 = num::Matrix::fromRows({{0.0, Complex{0.0, -1.0}}, {Complex{0.0, 1.0}, 0.0}});
    for (auto& v : k0.data)
        v *= std::sqrt(1.0 - p);
    for (auto& v : k1.data)
        v *= std::sqrt(p);
    gate.kraus = {k0, k1};
    RunView withGate = run;
    withGate.gate = gate;
    bench.hub->publish(withGate);
    auto process = acquire(bench.probe, "process_fidelity");
    REQUIRE(process);
    REQUIRE(process->y[0] == Approx(1.0 - p).epsilon(1e-12));
    REQUIRE(process->marker("F_avg")->value ==
            Approx((2.0 * (1.0 - p) + 1.0) / 3.0).epsilon(1e-12)); // T10 §1.3
    REQUIRE(process->marker("gate: x q0") != nullptr);
    REQUIRE(FidelityProbe::processFidelity(gate).value() == Approx(1.0 - p).epsilon(1e-12));
}

TEST_CASE("probe_trajectory, probe_thermal_truth and probe_leakage read the run and the fridge") {
    RunView run = bellRun();
    run.backend = qsim::Kind::Trajectories;
    run.jumps = {{1.5e-6, 0, "T1 q0"}, {4.0e-6, 1, "Tphi q1"}};
    {
        ProbeBench<TrajectoryProbe> bench(run);
        auto jumps = acquire(bench.probe, "jumps");
        REQUIRE(jumps);
        REQUIRE(jumps->simulatorOnly);
        REQUIRE(jumps->cls == FidelityClass::Statistical);
        REQUIRE(jumps->x == std::vector<double>{1.5e-6, 4.0e-6});
        REQUIRE(jumps->y == std::vector<double>{0.0, 1.0});
        REQUIRE(jumps->marker("T1 q0")->x == 1.5e-6);
    }
    {
        // Truth is the thermal snapshot itself; the reading column is empty without a thermometer.
        Environment env = instrtest::deviceEnvironment();
        env.thermal.T_K[static_cast<std::size_t>(cryo::stageIndex(cryo::Stage::MXC))] = 0.0123;
        ProbeBench<ThermalTruthProbe> bench(bellRun(), env);
        auto stages = acquire(bench.probe, "stages");
        REQUIRE(stages);
        REQUIRE(stages->simulatorOnly);
        REQUIRE(stages->cls == FidelityClass::Numerical);
        REQUIRE(stages->size() == cryo::kStages.size());
        const auto mxc = static_cast<std::size_t>(cryo::stageIndex(cryo::Stage::MXC));
        REQUIRE(stages->y[mxc] == 0.0123);
        REQUIRE(stages->marker("MXC")->value == 0.0123);
        REQUIRE(*bench.probe.query("stage.MXC.T") == 0.0123);
        REQUIRE(std::isnan(stages->aux.at("reading")[mxc]));
    }
    {
        // Three levels: the leakage trace is the summed |2> population per sample.
        RunView leaky = bellRun();
        leaky.levels = 3;
        leaky.populations = {{0.0, {1.0, 0.0, 0.0, 1.0, 0.0, 0.0}},
                             {2e-8, {0.9, 0.08, 0.02, 0.95, 0.04, 0.01}}};
        ProbeBench<LeakageProbe> bench(leaky);
        auto trace = acquire(bench.probe, "leakage");
        REQUIRE(trace);
        REQUIRE(trace->simulatorOnly);
        REQUIRE(trace->cls == FidelityClass::Numerical);
        REQUIRE(trace->y[0] == Approx(0.0).margin(1e-15));
        REQUIRE(trace->y[1] == Approx(0.03).epsilon(1e-12));
        REQUIRE(trace->aux.at("q0")[1] == Approx(0.02).epsilon(1e-12));
        REQUIRE(trace->aux.at("q1")[1] == Approx(0.01).epsilon(1e-12));
        REQUIRE(trace->marker("two-level run: no leakage levels") == nullptr);
    }
}

TEST_CASE("every probe is Simulator-only and absent from the Physical-lab workspace") {
    InstrumentRegistry registry;
    REQUIRE(registry.createStandardSet());
    const std::vector<std::string> probes = {"probe_state",         "probe_entanglement",
                                             "probe_fidelity",      "probe_trajectory",
                                             "probe_thermal_truth", "probe_leakage"};
    const auto all = registry.kinds();
    const auto physical = registry.kinds(true);
    for (auto const& kind : probes) {
        REQUIRE(std::find(all.begin(), all.end(), kind) != all.end());
        REQUIRE(std::find(physical.begin(), physical.end(), kind) == physical.end());
        IInstrument* probe = registry.find(kind);
        REQUIRE(probe != nullptr);
        REQUIRE(probe->simulatorOnly());
        for (auto const& channel : probe->channels())
            REQUIRE(channel.simulatorOnly); // spec 12 §12
    }
    for (IInstrument* i : registry.all(true))
        REQUIRE_FALSE(i->simulatorOnly());
    REQUIRE(registry.all().size() == registry.all(true).size() + probes.size());
    // No physical instrument claims the flag.
    for (IInstrument* i : registry.all())
        if (!i->id().kind.starts_with("probe_"))
            for (auto const& channel : i->channels())
                REQUIRE_FALSE(channel.simulatorOnly);

    // The binding path of a probe resolves in the simulator workspace and nowhere else.
    RunView run = bellRun();
    registry.inputs()->publish(run);
    registry.inputs()->publish(instrtest::deviceEnvironment());
    REQUIRE(*registry.query("probe.state.qubit[0].purity") == Approx(0.5).epsilon(1e-12));
    REQUIRE_FALSE(registry.query("probe.state.qubit[0].purity", true).has_value());
}
