// Spec 07 §7 — the backend selection table, the pinned-backend diagnostics and the factory.
#include "Circuits.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace qtest;

namespace {
SelectionRequest pulse(std::uint32_t n, std::uint32_t levels = 3) {
    SelectionRequest r;
    r.nQubits = n;
    r.levels = levels;
    r.pulseLevel = true;
    return r;
}
SelectionRequest gates(std::uint32_t n, bool noise = false, bool clifford = false, bool pauliOnly = false,
                       std::uint32_t levels = 2) {
    SelectionRequest r;
    r.nQubits = n;
    r.levels = levels;
    r.hasNoise = noise;
    r.cliffordOnly = clifford;
    r.pauliNoiseOnly = pauliOnly;
    return r;
}
Selection chosen(const SelectionRequest& r) {
    auto s = selectBackend(r);
    REQUIRE(s.has_value());
    REQUIRE_FALSE(s->reason.empty());
    return *s;
}
} // namespace

TEST_CASE("selectBackend follows every row of the spec 07 table") {
    const std::uint32_t svCap = StateVectorBackend::maxQubits();
    const std::uint32_t dmCap = DensityMatrixBackend::maxQubits();
    REQUIRE(svCap > dmCap);
    REQUIRE(dmCap <= 13);

    // Row 1: a pulse-level run up to 5 sites is exact-numerical Lindblad.
    for (std::uint32_t n = 1; n <= LindbladBackend::kMaxSites; ++n) {
        INFO("pulse-level " << n << " sites");
        const auto s = chosen(pulse(n));
        REQUIRE(s.kind == Kind::Lindblad);
        REQUIRE(s.cls == FidelityClass::Numerical);
        REQUIRE_FALSE(s.stochasticUnravelling);
    }
    // Row 2: above it, two-level sites up to 8 go to trajectories.
    for (std::uint32_t n = 6; n <= TrajectoriesBackend::kMaxSites; ++n) {
        INFO("pulse-level " << n << " qubits");
        const auto s = chosen(pulse(n, 2));
        REQUIRE(s.kind == Kind::Trajectories);
        REQUIRE(s.cls == FidelityClass::Statistical);
    }
    // Beyond both pulse-level caps the request is refused and the diagnostic states them.
    auto tooBig = selectBackend(pulse(9, 2));
    REQUIRE_FALSE(tooBig.has_value());
    REQUIRE(tooBig.error().code == err::TooLarge);
    REQUIRE(tooBig.error().message.find("Lindblad") != std::string::npos);
    REQUIRE(tooBig.error().message.find("trajectories") != std::string::npos);
    REQUIRE_FALSE(selectBackend(pulse(6, 3)).has_value()); // 3^6 > 243 and trajectories are two-level

    // Row 3: Clifford circuits go to the stabilizer backend, Exact without noise and Statistical with
    // Pauli-only noise; non-Pauli noise or a non-Clifford gate leaves the row.
    REQUIRE(chosen(gates(2000, false, true)).kind == Kind::Stabilizer);
    REQUIRE(chosen(gates(2000, false, true)).cls == FidelityClass::Exact);
    const auto pauliNoise = chosen(gates(500, true, true, true));
    REQUIRE(pauliNoise.kind == Kind::Stabilizer);
    REQUIRE(pauliNoise.cls == FidelityClass::Statistical);
    REQUIRE(chosen(gates(8, true, true, false)).kind == Kind::DensityMatrix); // non-Pauli noise
    // Beyond the tableau cap (10^4) the stabilizer row no longer applies, and no state vector can hold
    // 10001 qubits either, so the request is refused rather than silently mis-routed.
    auto beyondTableau = selectBackend(gates(10001, false, true));
    REQUIRE_FALSE(beyondTableau.has_value());
    REQUIRE(beyondTableau.error().code == err::TooLarge);

    // Row 4: no noise model, within the state-vector cap.
    REQUIRE(chosen(gates(svCap)).kind == Kind::StateVector);
    REQUIRE(chosen(gates(svCap)).cls == FidelityClass::Exact);
    REQUIRE_FALSE(chosen(gates(svCap)).stochasticUnravelling);
    // Row 5: a noise model within the density-matrix cap.
    const auto noisy = chosen(gates(dmCap, true));
    REQUIRE(noisy.kind == Kind::DensityMatrix);
    REQUIRE(noisy.cls == FidelityClass::Exact);
    // Row 6: a noise model above it falls back to per-shot stochastic unravelling.
    const auto unravel = chosen(gates(dmCap + 1, true));
    REQUIRE(unravel.kind == Kind::StateVector);
    REQUIRE(unravel.cls == FidelityClass::Statistical);
    REQUIRE(unravel.stochasticUnravelling);
    // Row 7: otherwise refuse, naming the caps in ascending order.
    auto refused = selectBackend(gates(svCap + 1, true));
    REQUIRE_FALSE(refused.has_value());
    REQUIRE(refused.error().code == err::TooLarge);
    const std::string msg = refused.error().message;
    REQUIRE(msg.find("density-matrix") < msg.find("state-vector")); // smallest violated cap first
    REQUIRE(selectBackend(gates(svCap + 1)).error().code == err::TooLarge);
    // A gate-level multi-level run matches no row and says which backend could be pinned.
    auto multi = selectBackend(gates(3, false, false, false, 3));
    REQUIRE_FALSE(multi.has_value());
    REQUIRE(multi.error().code == err::Unsupported);
    REQUIRE(multi.error().message.find("density-matrix") != std::string::npos);
    REQUIRE(chosen(gates(3, true, false, false, 3)).kind == Kind::DensityMatrix);
}

TEST_CASE("A pinned backend that cannot run the program fails loudly") {
    auto pinned = [](SelectionRequest r, Kind k) {
        r.pinned = k;
        return selectBackend(r);
    };
    // Pulse-level programs cannot run on the gate-level backends, and vice versa.
    for (Kind k : {Kind::StateVector, Kind::DensityMatrix, Kind::Stabilizer}) {
        auto s = pinned(pulse(2, 2), k);
        REQUIRE_FALSE(s.has_value());
        REQUIRE(s.error().code == err::Unsupported);
        REQUIRE(s.error().message.find(kindName(k)) != std::string::npos);
        REQUIRE(s.error().message.find("pulse") != std::string::npos);
    }
    for (Kind k : {Kind::Lindblad, Kind::Trajectories}) {
        auto s = pinned(gates(2), k);
        REQUIRE_FALSE(s.has_value());
        REQUIRE(s.error().code == err::Unsupported);
        REQUIRE(s.error().message.find("gate-level") != std::string::npos);
    }
    // Capability mismatches.
    REQUIRE(pinned(gates(4), Kind::Stabilizer).error().code == err::Unsupported);               // not Clifford
    REQUIRE(pinned(gates(4, true, true, false), Kind::Stabilizer).error().code == err::Unsupported); // non-Pauli noise
    REQUIRE(pinned(gates(4, false, true, false, 3), Kind::Stabilizer).error().code == err::Unsupported);
    REQUIRE(pinned(gates(4, false, false, false, 3), Kind::StateVector).error().code == err::Unsupported);
    // Size caps, with the same diagnostic the automatic choice would give.
    REQUIRE(pinned(gates(StateVectorBackend::maxQubits() + 1), Kind::StateVector).error().code == err::TooLarge);
    REQUIRE(pinned(gates(DensityMatrixBackend::maxQubits() + 1, true), Kind::DensityMatrix).error().code == err::TooLarge);
    REQUIRE(pinned(gates(9, false, false, false, 3), Kind::DensityMatrix).error().code == err::TooLarge); // 3^9 > 8192
    REQUIRE(pinned(gates(10001, false, true), Kind::Stabilizer).error().code == err::TooLarge);
    REQUIRE(pinned(pulse(6), Kind::Lindblad).error().code == err::TooLarge);
    REQUIRE(pinned(pulse(4, 4), Kind::Lindblad).error().code == err::TooLarge);  // 4^4 = 256 > 243
    REQUIRE(pinned(pulse(9, 2), Kind::Trajectories).error().code == err::TooLarge);
    REQUIRE(pinned(pulse(6, 3), Kind::Trajectories).error().code == err::TooLarge); // 3^6 > 256
    // Pins that can run keep the requested backend and carry the right fidelity class.
    struct Case { SelectionRequest req; Kind kind; FidelityClass cls; bool unravel; };
    const std::vector<Case> ok{
        {gates(6), Kind::StateVector, FidelityClass::Exact, false},
        {gates(6, true), Kind::StateVector, FidelityClass::Statistical, true},
        {gates(6, true), Kind::DensityMatrix, FidelityClass::Exact, false},
        {gates(6, false, false, false, 3), Kind::DensityMatrix, FidelityClass::Exact, false},
        {gates(6, false, true), Kind::Stabilizer, FidelityClass::Exact, false},
        {gates(6, true, true, true), Kind::Stabilizer, FidelityClass::Statistical, false},
        {pulse(5), Kind::Lindblad, FidelityClass::Numerical, false},
        {pulse(8, 2), Kind::Trajectories, FidelityClass::Statistical, false},
        {pulse(3, 3), Kind::Trajectories, FidelityClass::Statistical, false}};
    for (const auto& c : ok) {
        SelectionRequest r = c.req;
        r.pinned = c.kind;
        INFO(kindName(c.kind) << " on " << r.nQubits << " sites");
        auto s = selectBackend(r);
        REQUIRE(s.has_value());
        REQUIRE(s->kind == c.kind);
        REQUIRE(s->cls == c.cls);
        REQUIRE(s->stochasticUnravelling == c.unravel);
        REQUIRE(s->reason.find("pinned") != std::string::npos);
    }
}

TEST_CASE("makeBackend builds every kind with the capabilities of spec 07") {
    struct Expected { Kind kind; bool exactNoise, stochasticNoise, nonClifford, midCircuit, multiLevel, timeDomain, readback; };
    const std::vector<Expected> table{
        {Kind::StateVector, false, true, true, true, false, false, true},
        {Kind::DensityMatrix, true, false, true, true, true, false, true},
        {Kind::Stabilizer, false, true, false, true, false, false, true},
        {Kind::Lindblad, true, false, true, true, true, true, true},
        {Kind::Trajectories, false, true, true, true, true, true, true}};
    for (const auto& e : table) {
        INFO(kindName(e.kind));
        auto b = makeBackend(e.kind);
        REQUIRE(b != nullptr);
        REQUIRE(b->kind() == e.kind);
        const Capabilities c = b->capabilities();
        REQUIRE(c.kind == e.kind);
        REQUIRE(c.maxQubits > 0);
        REQUIRE(c.exactNoise == e.exactNoise);
        REQUIRE(c.stochasticNoise == e.stochasticNoise);
        REQUIRE(c.nonClifford == e.nonClifford);
        REQUIRE(c.midCircuitMeasure == e.midCircuit);
        REQUIRE(c.multiLevel == e.multiLevel);
        REQUIRE(c.timeDomain == e.timeDomain);
        REQUIRE(c.fullStateReadback == e.readback);
        REQUIRE(b->opCount() == 0);
        REQUIRE(kindName(e.kind) == kindName(c.kind));
    }
}
