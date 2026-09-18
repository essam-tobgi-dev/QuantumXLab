// Spec 07 §4, §9, T09 §3 — stabilizer tableau backend against the state vector: random Clifford
// circuits, Clifford recognition and non-Clifford refusal.
#include "Circuits.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using namespace qtest;
using Catch::Approx;

namespace {
// The 24 single-qubit Cliffords up to global phase, generated as words in H and S (T09 §1.3).
Matrix canonicalPhase(Matrix m) {
    Complex lead = 1.0;
    for (const auto& v : m.data)
        if (std::abs(v) > 1e-9) { lead = v / std::abs(v); break; }
    for (auto& w : m.data) w /= lead;
    return m;
}

std::vector<Matrix> singleQubitCliffords() {
    std::vector<Matrix> out{canonicalPhase(I2())};
    std::vector<Matrix> frontier = out;
    auto known = [&out](const Matrix& m) {
        return std::any_of(out.begin(), out.end(), [&m](const Matrix& e) { return maxAbsDiff(e, m) < 1e-9; });
    };
    while (!frontier.empty()) {
        std::vector<Matrix> next;
        for (const auto& m : frontier)
            for (const auto& gen : {H(), S()}) {
                Matrix p = canonicalPhase(num::matmul(gen, m));
                if (!known(p)) { out.push_back(p); next.push_back(p); }
            }
        frontier = std::move(next);
    }
    return out;
}

std::size_t keyToIndex(const std::string& key) {
    std::size_t idx = 0;
    for (std::size_t b = 0; b < key.size(); ++b)
        if (key[key.size() - 1 - b] == '1') idx |= std::size_t{1} << b;
    return idx;
}
} // namespace

TEST_CASE("Stabilizer reproduces the state-vector distribution on random Clifford circuits") {
    const std::uint32_t n = 8;
    for (std::uint64_t seed : {11u, 22u, 33u, 44u}) {
        INFO("seed " << seed);
        const Circuit c = randomClifford(n, 80, seed);
        StateVectorBackend sv;
        StabilizerBackend st;
        REQUIRE(sv.allocate(n).has_value());
        REQUIRE(st.allocate(n).has_value());
        REQUIRE(applyAll(sv, c).has_value());
        REQUIRE(applyAll(st, c).has_value());
        // Spec 07 §9: outcome probabilities agree exactly — the stabilizer distribution is a
        // uniform 2^-k pattern and the state vector must reproduce it to round-off.
        auto pSv = sv.probabilities(allQubits(n));
        auto pSt = st.probabilities(allQubits(n));
        REQUIRE(pSv.has_value());
        REQUIRE(pSt.has_value());
        REQUIRE(pSt->size() == pSv->size());
        std::size_t support = 0;
        for (std::size_t i = 0; i < pSv->size(); ++i) {
            INFO("basis index " << i);
            REQUIRE((*pSt)[i] == Approx((*pSv)[i]).margin(1e-12));
            if ((*pSv)[i] > 1e-12) ++support;
        }
        REQUIRE(support > 0);
        REQUIRE((support & (support - 1)) == 0); // 2^k equally likely outcomes
        // Deterministic single-qubit outcomes (⟨Z_q⟩ = ±1 on the state vector) agree bit-exactly and
        // are reported with probability exactly 1; the others are fair coins.
        core::Random rng(seed * 7919 + 1);
        for (std::uint32_t qb = 0; qb < n; ++qb) {
            std::string label(n, 'I');
            label[n - 1 - qb] = 'Z';
            const double ez = sv.expectation(*PauliString::parse(label)).value();
            REQUIRE(st.expectation(*PauliString::parse(label)).value() == Approx(ez).margin(1e-12));
            auto copy = st.clone();
            auto out = copy->measure(q({qb}), rng);
            REQUIRE(out.has_value());
            if (std::abs(std::abs(ez) - 1.0) < 1e-12) {
                REQUIRE(out->probability == 1.0);
                REQUIRE(out->bits[0] == (ez < 0 ? 1 : 0));
            } else {
                REQUIRE(std::abs(ez) < 1e-12);
                REQUIRE(out->probability == 0.5);
            }
        }
        // Random outcomes agree statistically: 2000 seeded shots, 5σ for every basis state
        // (an outcome the state vector forbids must never appear).
        const std::uint64_t shots = 2000;
        auto counts = st.sample(allQubits(n), shots, rng);
        REQUIRE(counts.has_value());
        std::uint64_t total = 0;
        for (const auto& [key, v] : *counts) {
            INFO("bitstring " << key);
            total += v;
            REQUIRE((*pSv)[keyToIndex(key)] > 1e-12);
        }
        REQUIRE(total == shots);
        for (std::size_t i = 0; i < pSv->size(); ++i) {
            if ((*pSv)[i] < 1e-12) continue;
            std::string key(n, '0');
            for (std::size_t b = 0; b < n; ++b) if ((i >> b) & 1) key[n - 1 - b] = '1';
            const auto it = counts->find(key);
            INFO("bitstring " << key);
            REQUIRE(withinSigma(it == counts->end() ? 0 : it->second, shots, (*pSv)[i], 5.0));
        }
        // Spec 07 §8: tableau-rank entanglement entropy equals the von Neumann entropy of ρ_A.
        for (const auto& sub : {q({0}), q({2, 5}), q({1, 3, 6, 7})}) {
            auto rhoA = sv.reducedDensityMatrix(sub);
            REQUIRE(rhoA.has_value());
            REQUIRE(st.entanglementEntropy(sub) == Approx(measures::entropyBits(*rhoA)).margin(1e-9));
        }
    }
}

TEST_CASE("Stabilizer refuses non-Clifford gates and names the gate") {
    StabilizerBackend st;
    REQUIRE(st.allocate(3).has_value());
    auto direct = st.applyGate(T(), q({0}));
    REQUIRE_FALSE(direct.has_value());
    REQUIRE(direct.error().code == err::NotClifford);                     // ErrorCode::QSim_ + 5
    REQUIRE(static_cast<std::uint32_t>(direct.error().code) >= static_cast<std::uint32_t>(ErrorCode::QSim_));
    REQUIRE(static_cast<std::uint32_t>(direct.error().code) < static_cast<std::uint32_t>(ErrorCode::Noise_));
    // Through the GateOp entry point the diagnostic names the offending gate (spec 07 §4, §10).
    GateOp op{T(), q({0}), {}, GateClass::Generic, "t", 17};
    auto named = st.apply(op);
    REQUIRE_FALSE(named.has_value());
    REQUIRE(named.error().code == err::NotClifford);
    REQUIRE(named.error().format().find("'t'") != std::string::npos);
    REQUIRE(named.error().format().find("op #17") != std::string::npos);
    // A refused gate leaves the tableau untouched.
    REQUIRE(st.expectation(*PauliString::parse("IIZ")).value() == 1.0);
    // A generic rotation is rejected; a rotation on a Clifford angle is accepted (spec 07 §4).
    REQUIRE(st.applyGate(RZ(0.3), q({1})).error().code == err::NotClifford);
    REQUIRE(st.applyGate(RX(1.0), q({1})).error().code == err::NotClifford);
    REQUIRE(st.applyGate(RZ(std::numbers::pi / 2), q({1})).has_value());
    REQUIRE(st.applyGate(RX(std::numbers::pi), q({1})).has_value());
    REQUIRE(st.expectation(*PauliString::parse("IZI")).value() == -1.0);
    // Kraus channels belong to the density-matrix backend; Pauli frames go through applyPauli.
    REQUIRE(st.applyChannel(depolarizing(0.1), q({0})).error().code == err::Unsupported);
    REQUIRE(st.allocate(2, 3).error().code == err::Unsupported);
    REQUIRE(st.allocate(20000).error().code == err::TooLarge);
    REQUIRE(st.capabilities().nonClifford == false);
    REQUIRE(st.capabilities().midCircuitMeasure);
}

TEST_CASE("Clifford recognition accepts all 24 single-qubit Cliffords up to global phase") {
    const std::vector<Matrix> cliffords = singleQubitCliffords();
    REQUIRE(cliffords.size() == 24);
    for (std::size_t i = 0; i < cliffords.size(); ++i) {
        INFO("Clifford #" << i);
        for (double theta : {0.0, 0.7, 2.3}) { // an arbitrary global phase must not matter
            Matrix u = cliffords[i];
            for (auto& v : u.data) v *= std::exp(Complex(0, theta));
            auto img = StabilizerBackend::cliffordImages(u, 1);
            REQUIRE(img.has_value());
            REQUIRE(img->size() == 2);
        }
        // The tableau update reproduces the state vector for this gate on a non-trivial state.
        StateVectorBackend sv;
        StabilizerBackend st;
        REQUIRE(sv.allocate(2).has_value());
        REQUIRE(st.allocate(2).has_value());
        for (IBackend* b : {static_cast<IBackend*>(&sv), static_cast<IBackend*>(&st)}) {
            REQUIRE(b->applyGate(H(), q({0})).has_value());
            REQUIRE(b->applyGate(CX(), q({0, 1})).has_value());
            REQUIRE(b->applyGate(cliffords[i], q({0})).has_value());
        }
        for (const char* label : {"II", "IX", "IY", "IZ", "XI", "XX", "YY", "ZZ", "ZI", "XZ", "ZX", "YX"}) {
            INFO(label);
            auto ps = PauliString::parse(label);
            REQUIRE(ps.has_value());
            REQUIRE(st.expectation(*ps).value() == Approx(sv.expectation(*ps).value()).margin(1e-12));
        }
    }
    // A generic rotation is not in the Clifford group.
    REQUIRE(StabilizerBackend::cliffordImages(RY(0.4), 1).error().code == err::NotClifford);
    REQUIRE(StabilizerBackend::cliffordImages(T(), 1).error().code == err::NotClifford);
}
