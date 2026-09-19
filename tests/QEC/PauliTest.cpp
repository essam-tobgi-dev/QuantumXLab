// Spec 16 §1, T09 §1.1 — Pauli strings: text order (qubit 0 first), products with phase, the
// symplectic form T09 (1.2), F2 rank and group membership, conversion to qsim labels.
#include "QEC/Pauli.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace qlab;
using namespace qlab::qec;
using Catch::Approx;

namespace {
PauliString P(std::string_view s) {
    auto r = PauliString::parse(s);
    REQUIRE(r.has_value());
    return *r;
}
} // namespace

TEST_CASE("Pauli: text is written from qubit 0 and round-trips") {
    const PauliString p = P("XZZXI");
    REQUIRE(p.n == 5);
    REQUIRE(p.letter(0) == 'X');
    REQUIRE(p.letter(1) == 'Z');
    REQUIRE(p.letter(3) == 'X');
    REQUIRE(p.letter(4) == 'I');
    REQUIRE(p.weight() == 4);
    REQUIRE(p.support() == std::vector<std::uint32_t>{0, 1, 2, 3});
    REQUIRE(p.str() == "XZZXI");
    REQUIRE(P("-iXY").str(true) == "-iXY");
    REQUIRE(P("-ZZ").phase == 2);
    REQUIRE(P("iZ").phase == 1);
    // qsim labels are most-significant qubit first: the same operator reads reversed.
    REQUIRE(p.toQsim().label() == "IXZZX");
    REQUIRE(p.toQsim().op(0) == 'X');
    REQUIRE(P("-XI").toQsim().phase().real() == Approx(-1.0));
}

TEST_CASE("Pauli: malformed strings are rejected with the offending letter") {
    auto bad = PauliString::parse("XQZ");
    REQUIRE_FALSE(bad.has_value());
    REQUIRE(bad.error().code == err::BadPauli);
    REQUIRE(bad.error().message.find("'Q'") != std::string::npos);
    REQUIRE(bad.error().message.find("qubit 1") != std::string::npos);
    REQUIRE_FALSE(PauliString::parse("").has_value());
    REQUIRE_FALSE(PauliString::parse("-").has_value());
}

TEST_CASE("Pauli: single-qubit products follow XY = iZ and its cyclic images") {
    // T09 §1.1: XY = iZ, YZ = iX, ZX = iY; the reversed products carry −i.
    REQUIRE((P("X") * P("Y")) == P("iZ"));
    REQUIRE((P("Y") * P("Z")) == P("iX"));
    REQUIRE((P("Z") * P("X")) == P("iY"));
    REQUIRE((P("Y") * P("X")) == P("-iZ"));
    REQUIRE((P("Z") * P("Y")) == P("-iX"));
    REQUIRE((P("X") * P("Z")) == P("-iY"));
    for (const char* l : {"X", "Y", "Z"})
        REQUIRE((P(l) * P(l)) == P("I"));
    // Phases multiply: (−X)(iY) = −i · (iZ) = +Z, and (iX)(iZ) = −(−iY) = iY.
    REQUIRE((P("-X") * P("iY")) == P("Z"));
    REQUIRE((P("iX") * P("iZ")) == P("iY"));
}

TEST_CASE("Pauli: multi-qubit product and commutation agree with the symplectic form") {
    // Five-qubit code generators (T09 (4.3)) commute pairwise; XZZXI · IXZZX = XYIYX up to phase.
    const PauliString g0 = P("XZZXI"), g1 = P("IXZZX");
    REQUIRE(g0.commutesWith(g1));
    const PauliString prod = g0 * g1;
    REQUIRE(prod.str() == "XYIYX");
    REQUIRE(prod.isHermitian());
    // Commuting Hermitian strings multiply to the same operator in either order.
    REQUIRE(prod == g1 * g0);
    // X0 anticommutes with Z0, commutes with Z1.
    REQUIRE_FALSE(P("XI").commutesWith(P("ZI")));
    REQUIRE(P("XI").commutesWith(P("IZ")));
    // Anticommuting strings: PQ = −QP.
    const PauliString a = P("XX"), b = P("ZI");
    REQUIRE_FALSE(a.commutesWith(b));
    REQUIRE(((a * b).phase - (b * a).phase + 4) % 4 == 2);
    // Y counts once in the symplectic form: Y anticommutes with X and with Z.
    REQUIRE_FALSE(P("Y").commutesWith(P("X")));
    REQUIRE_FALSE(P("Y").commutesWith(P("Z")));
    REQUIRE(P("YY").commutesWith(P("XX")));
}

TEST_CASE("Pauli: type predicates, syndrome, rank and group membership") {
    REQUIRE(P("XIX").isXType());
    REQUIRE_FALSE(P("XIY").isXType());
    REQUIRE(P("ZZI").isZType());
    REQUIRE(P("III").isXType());
    REQUIRE(P("III").isIdentity());

    // Bit-flip code T09 §4.1: generators Z0Z1, Z1Z2; the syndrome table of the theory document.
    const std::vector<PauliString> gens = {P("ZZI"), P("IZZ")};
    REQUIRE(syndromeOf(gens, P("XII")) == std::vector<std::uint8_t>{1, 0});
    REQUIRE(syndromeOf(gens, P("IXI")) == std::vector<std::uint8_t>{1, 1});
    REQUIRE(syndromeOf(gens, P("IIX")) == std::vector<std::uint8_t>{0, 1});
    REQUIRE(syndromeOf(gens, P("ZII")) == std::vector<std::uint8_t>{0, 0});

    REQUIRE(symplecticRank(gens) == 2);
    const std::vector<PauliString> dependent = {P("ZZI"), P("IZZ"), P("ZIZ")};
    REQUIRE(symplecticRank(dependent) == 2);

    // Z0Z2 = (Z0Z1)(Z1Z2) is in the group with sign +1; −Z0Z2 has sign −1; X0 is outside.
    int sign = 0;
    REQUIRE(inGroup(gens, P("ZIZ"), &sign));
    REQUIRE(sign == 1);
    REQUIRE(inGroup(gens, P("-ZIZ"), &sign));
    REQUIRE(sign == -1);
    REQUIRE_FALSE(inGroup(gens, P("XII")));
    auto subset = solveProduct(gens, P("ZIZ"));
    REQUIRE(subset.has_value());
    REQUIRE(*subset == std::vector<std::uint32_t>{0, 1});
    // Steane X-type generators: the product of all three is X on qubits {0,1,3,6} (odd-weight
    // columns of (4.2)): columns 1,2,4,7 of the Hamming matrix have odd weight.
    const std::vector<PauliString> steaneX = {P("IIIXXXX"), P("IXXIIXX"), P("XIXIXIX")};
    auto all = solveProduct(steaneX, P("XXIXIIX"));
    REQUIRE(all.has_value());
    REQUIRE(all->size() == 3);
}

TEST_CASE("Wilson interval matches the closed form") {
    // k = 0: lower bound 0, upper bound z²/(n + z²).
    const Interval zero = wilsonInterval(0, 1000, 2.0);
    REQUIRE(zero.lo == Approx(0.0).margin(1e-15));
    REQUIRE(zero.hi == Approx(4.0 / 1004.0).epsilon(1e-12));
    // p̂ = 1/2, n = 100, z = 2: centre 1/2, half width z·sqrt(1/400 + 4/40000)/(1 + 4/100).
    const Interval half = wilsonInterval(50, 100, 2.0);
    REQUIRE(half.center == Approx(0.5).epsilon(1e-12));
    REQUIRE(half.hi - half.center ==
            Approx(2.0 * std::sqrt(0.0025 + 0.0001) / 1.04).epsilon(1e-12));
    REQUIRE(half.center - half.lo == Approx(half.hi - half.center).epsilon(1e-12));
    const Interval none = wilsonInterval(0, 0);
    REQUIRE(none.lo == 0.0);
    REQUIRE(none.hi == 1.0);
}
