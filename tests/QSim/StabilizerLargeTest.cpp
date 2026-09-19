// Spec 07 §4, T09 §1.2 — stabilizer backend on large registers (wider than a 64-bit mask), Bell
// generators, tableau export, clone independence and argument validation.
#include "Circuits.hpp"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace qtest;
using Catch::Approx;

namespace {
// MSB-first label of an n-qubit Pauli string with the given letters (index = qubit).
std::string wideLabel(std::uint32_t n,
                      std::initializer_list<std::pair<std::uint32_t, char>> letters,
                      char fill = 'I') {
    std::string s(n, fill);
    for (auto [qb, c] : letters)
        s[n - 1 - qb] = c;
    return s;
}
} // namespace

TEST_CASE("Stabilizer: a 2000-qubit GHZ state measures all-equal bits") {
    const std::uint32_t n = 2000;
    StabilizerBackend st;
    REQUIRE(st.allocate(n).has_value());
    REQUIRE(st.applyGate(H(), q({0})).has_value());
    for (std::uint32_t i = 0; i + 1 < n; ++i) // the IR class fast path (spec 07 §2.2 dispatch)
        REQUIRE(st.apply(GateOp{CX(), q({i, i + 1}), {}, GateClass::Cnot, "cx", i}).has_value());
    // Every qubit, two independent shots: the first bit is random, the other 1999 follow it.
    const auto everyQubit = allQubits(n);
    core::Random rng(20259);
    for (int shot = 0; shot < 2; ++shot) {
        auto copy = st.clone();
        auto out = copy->measure(everyQubit, rng);
        REQUIRE(out.has_value());
        REQUIRE(out->bits.size() == n);
        const auto first = out->bits[0];
        REQUIRE(std::all_of(out->bits.begin(), out->bits.end(),
                            [first](std::uint8_t b) { return b == first; }));
        REQUIRE(out->probability == 0.5);
    }
    // Expectations of Pauli strings wider than 64 qubits (the former 64-bit mask overflow): the GHZ
    // generators of T09 §1.2 and products of them.
    auto zz = PauliString::fromQubits(
        n, std::vector<std::pair<QubitIndex, char>>{{QubitIndex{5}, 'Z'}, {QubitIndex{1234}, 'Z'}});
    REQUIRE(st.expectation(zz).value() == 1.0);
    auto zzParsed = PauliString::parse(wideLabel(n, {{5, 'Z'}, {1234, 'Z'}}));
    REQUIRE(zzParsed.has_value());
    REQUIRE(zzParsed->label() == zz.label());
    REQUIRE(st.expectation(*zzParsed).value() == 1.0);
    REQUIRE(st.expectation(*PauliString::parse(wideLabel(n, {{1999, 'Z'}}))).value() == 0.0);
    REQUIRE(st.expectation(*PauliString::parse(wideLabel(n, {}, 'X'))).value() == 1.0);
    // Y⊗Y on two GHZ qubits among X's: i·i = −1 on both branches.
    REQUIRE(
        st.expectation(*PauliString::parse(wideLabel(n, {{0, 'Y'}, {1777, 'Y'}}, 'X'))).value() ==
        -1.0);
    REQUIRE(
        st.expectation(*PauliString::parse("-" + wideLabel(n, {{70, 'Z'}, {1500, 'Z'}}))).value() ==
        -1.0);
    REQUIRE_FALSE(zz.isIdentity());
    REQUIRE(PauliString::parse(wideLabel(n, {{1900, 'X'}}))->isIdentity() == false);
    REQUIRE(st.entanglementEntropy(q({0, 1, 2})) == Approx(1.0).margin(1e-12));
    REQUIRE(std::isnan(st.entanglementEntropy(q({3, 3}))));
    REQUIRE(std::isnan(st.entanglementEntropy(q({n}))));
    REQUIRE(st.bytesAllocated() >=
            std::size_t{2} * n * 2 * ((n + 63) / 64) * sizeof(std::uint64_t));
}

TEST_CASE("Stabilizer: Bell generators, tableau export and clone independence") {
    StabilizerBackend bell;
    REQUIRE(bell.allocate(2).has_value());
    REQUIRE(bell.applyGate(H(), q({0})).has_value());
    REQUIRE(bell.applyGate(CX(), q({0, 1})).has_value());
    // T09 §1.2: |Φ+⟩ is stabilized by X₁X₀ and Z₁Z₀.
    TableauExport tab = bell.exportTableau();
    REQUIRE(tab.n == 2);
    REQUIRE(tab.stabilizers.size() == 2);
    REQUIRE(tab.destabilizers.size() == 2);
    REQUIRE(tab.stabilizers[0] == "+XX");
    REQUIRE(tab.stabilizers[1] == "+ZZ");
    REQUIRE(bell.expectation(*PauliString::parse("XX")).value() == Approx(1.0).margin(1e-12));
    REQUIRE(bell.expectation(*PauliString::parse("ZZ")).value() == Approx(1.0).margin(1e-12));
    REQUIRE(bell.expectation(*PauliString::parse("YY")).value() == Approx(-1.0).margin(1e-12));
    REQUIRE(bell.expectation(*PauliString::parse("ZI")).value() == Approx(0.0).margin(1e-12));
    REQUIRE(bell.entanglementEntropy(q({0})) == Approx(1.0).margin(1e-12));
    // |Φ−⟩ = Z₀|Φ+⟩ flips the sign of the XX generator only; |Ψ+⟩ = X₀|Φ+⟩ flips ZZ; Y₀ flips both.
    StabilizerBackend minus = bell;
    minus.z(0);
    REQUIRE(minus.exportTableau().stabilizers == std::vector<std::string>{"-XX", "+ZZ"});
    REQUIRE(minus.expectation(*PauliString::parse("XX")).value() == Approx(-1.0).margin(1e-12));
    StabilizerBackend psi = bell;
    psi.x(0);
    REQUIRE(psi.exportTableau().stabilizers == std::vector<std::string>{"+XX", "-ZZ"});
    StabilizerBackend both = bell;
    both.y(1);
    REQUIRE(both.exportTableau().stabilizers == std::vector<std::string>{"-XX", "-ZZ"});
    // clone() is a deep copy: measuring the clone must not touch the original.
    auto copy = bell.clone();
    core::Random rng(4242);
    auto m = copy->measure(q({0, 1}), rng);
    REQUIRE(m.has_value());
    REQUIRE(m->bits[0] == m->bits[1]);
    REQUIRE(m->probability == 0.5);
    REQUIRE(bell.exportTableau().stabilizers == tab.stabilizers);
    REQUIRE(bell.expectation(*PauliString::parse("XX")).value() == Approx(1.0).margin(1e-12));
    // The clone collapsed onto |bb⟩: Z₀ and Z₁ are ±1 with the measured sign, XX is gone. The
    // Aaronson–Gottesman update replaces the XX generator by ±Z₀ ("IZ", MSB-first) and keeps ZZ.
    const double sign = m->bits[0] ? -1.0 : 1.0;
    REQUIRE(copy->expectation(*PauliString::parse("IZ")).value() == sign);
    REQUIRE(copy->expectation(*PauliString::parse("ZI")).value() == sign);
    REQUIRE(copy->expectation(*PauliString::parse("XX")).value() == 0.0);
    const auto* collapsed = static_cast<StabilizerBackend*>(copy.get());
    auto after = collapsed->exportTableau();
    REQUIRE(after.stabilizers[0] == (m->bits[0] ? "-IZ" : "+IZ"));
    REQUIRE(after.stabilizers[1] == "+ZZ");
    REQUIRE(collapsed->entanglementEntropy(q({0})) == 0.0);
}

TEST_CASE("Stabilizer: argument validation, empty requests and reset") {
    StabilizerBackend fresh;
    core::Random rng(3);
    REQUIRE(fresh.applyGate(H(), q({0})).error().code == err::NotAllocated);
    REQUIRE(fresh.reset(q({0}), rng).error().code == err::NotAllocated);
    REQUIRE(fresh.expectation(*PauliString::parse("Z")).error().code == err::NotAllocated);
    StabilizerBackend st;
    REQUIRE(st.allocate(3).has_value());
    REQUIRE(st.reset(q({3}), rng).error().code == err::BadTargets);
    REQUIRE(st.measure(q({0, 7}), rng).error().code == err::BadTargets);
    REQUIRE(st.probabilities(q({5})).error().code == err::BadTargets);
    REQUIRE(st.sample(q({1, 1}), 4, rng).error().code == err::BadTargets);
    REQUIRE(st.applyControlled(X(), q({2}), q({2})).error().code == err::BadTargets);
    REQUIRE(st.applyControlled(CX(), q({2}), q({0})).error().code == err::BadTargets);
    // A class tag that does not fit the op's shape is not trusted: the matrix decides.
    REQUIRE(st.apply(GateOp{X(), q({1}), {}, GateClass::Cnot, "bad", 1}).has_value());
    REQUIRE(st.expectation(*PauliString::parse("IZI")).value() == -1.0);
    // An empty qubit list means the whole register, in index order (as on every other backend).
    REQUIRE(st.applyGate(H(), q({0})).has_value());
    auto pAll = st.probabilities({});
    REQUIRE(pAll.has_value());
    REQUIRE(pAll->size() == 8);
    REQUIRE((*pAll)[0b010] == 0.5);
    REQUIRE((*pAll)[0b011] == 0.5);
    core::Random a(77), b(77);
    auto cEmpty = st.sample({}, 64, a);
    auto cAll = st.sample(allQubits(3), 64, b);
    REQUIRE(cEmpty.has_value());
    REQUIRE(*cEmpty == *cAll);
    REQUIRE(cEmpty->size() == 2);
    REQUIRE(cEmpty->contains("010"));
    // reset is measure-then-flip: every qubit returns to |0⟩ whatever the branch.
    for (std::uint64_t seed = 0; seed < 8; ++seed) {
        StabilizerBackend r = st;
        core::Random rr(seed);
        REQUIRE(r.reset(allQubits(3), rr).has_value());
        for (const char* label : {"IIZ", "IZI", "ZII"})
            REQUIRE(r.expectation(*PauliString::parse(label)).value() == 1.0);
    }
}
