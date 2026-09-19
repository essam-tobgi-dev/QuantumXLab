// Spec 04 §6 / spec 25 §9 — determinism: identical seeds give bit-identical samples on every
// gate-level backend, and different seeds give different samples. The trajectories backend is
// covered in TrajectoriesTest.cpp.
#include "Circuits.hpp"
#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace qtest;

namespace {
template <class Backend>
Counts sampled(const Circuit& c, std::uint32_t n, std::uint64_t seed, std::uint64_t shots) {
    Backend b;
    REQUIRE(b.allocate(n).has_value());
    REQUIRE(applyAll(b, c).has_value());
    core::Random rng(seed);
    const auto qs = allQubits(n);
    auto r = b.sample(qs, shots, rng);
    REQUIRE(r.has_value());
    return *r;
}

template <class Backend> void checkSeeded(const Circuit& c, std::uint32_t n) {
    const Counts a = sampled<Backend>(c, n, 1234, 4000);
    const Counts b = sampled<Backend>(c, n, 1234, 4000);
    const Counts d = sampled<Backend>(c, n, 1235, 4000);
    REQUIRE(a == b); // exact equality of the whole histogram, not a statistical comparison
    REQUIRE(a != d);
    std::uint64_t total = 0;
    for (const auto& [bits, k] : a) {
        REQUIRE(bits.size() == n);
        total += k;
    }
    REQUIRE(total == 4000);
}
} // namespace

TEST_CASE("Determinism: the state-vector backend samples bit-identically per seed") {
    checkSeeded<StateVectorBackend>(randomUniversal(6, 60, 77), 6);
}

TEST_CASE("Determinism: the density-matrix backend samples bit-identically per seed") {
    checkSeeded<DensityMatrixBackend>(randomUniversal(5, 40, 78), 5);
}

TEST_CASE("Determinism: the stabilizer backend samples bit-identically per seed") {
    checkSeeded<StabilizerBackend>(randomClifford(8, 80, 79), 8);
}

TEST_CASE("Determinism: mid-circuit measurement outcomes follow the seed") {
    // The measured bit and the collapsed state must both be a function of the seed alone.
    auto run = [](std::uint64_t seed) {
        StateVectorBackend b;
        REQUIRE(b.allocate(3).has_value());
        REQUIRE(applyAll(b, randomUniversal(3, 25, 80)).has_value());
        core::Random rng(seed);
        std::vector<std::uint8_t> bits;
        for (std::uint32_t k = 0; k < 3; ++k) {
            const auto qs = q({k});
            auto m = b.measure(qs, rng);
            REQUIRE(m.has_value());
            bits.insert(bits.end(), m->bits.begin(), m->bits.end());
        }
        return bits;
    };
    REQUIRE(run(5) == run(5));
    bool differs = false;
    for (std::uint64_t s = 6; s < 40 && !differs; ++s)
        differs = run(s) != run(5);
    REQUIRE(differs);
}
