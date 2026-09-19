// Spec 14 §5.5, T02 §6 — Cartan (KAK) decomposition: exact reconstruction of random SU(4) and of
// the library gates, their known canonical coordinates and minimal cx counts, synthesis over
// {U, cx}, and block resynthesis that lowers the two-qubit gate count without changing the unitary.
#include "CompilerTestUtil.hpp"
#include "Numerics/Tensor.hpp"
#include <algorithm>
#include <cmath>

using namespace ctest;

namespace {
num::Matrix gateMatrix(std::string_view name, std::vector<double> p = {}) {
    return *ir::gates::matrix(name, p);
}

// Random two-qubit unitary: a product of random local gates and entanglers.
num::Matrix randomUnitary(core::Random& rng) {
    std::vector<ir::Gate> gs;
    for (int layer = 0; layer < 4; ++layer) {
        gs.push_back(G("U", {0}, {angle(rng), angle(rng), angle(rng)}));
        gs.push_back(G("U", {1}, {angle(rng), angle(rng), angle(rng)}));
        gs.push_back(G(layer % 2 ? "cx" : "rzz", {0, 1},
                       layer % 2 ? std::vector<double>{} : std::vector<double>{angle(rng)}));
    }
    return unitary(circuit(2, gs));
}
// |a| ≥ |b| ≥ |c| of a decomposition, for comparison with the Weyl-chamber table of T02 §6.
std::array<double, 3> sorted(const compiler::KakDecomposition& k) {
    std::array<double, 3> v{std::abs(k.a), std::abs(k.b), std::abs(k.c)};
    std::sort(v.rbegin(), v.rend());
    return v;
}
} // namespace

TEST_CASE("canonical gate: exp(i(aXX + bYY + cZZ)) = rxx(−2a)·ryy(−2b)·rzz(−2c)") {
    core::Random rng(3);
    for (int trial = 0; trial < 20; ++trial) {
        const double a = angle(rng), b = angle(rng), c = angle(rng);
        const num::Matrix want =
            num::matmul(num::matmul(gateMatrix("rxx", {-2 * a}), gateMatrix("ryy", {-2 * b})),
                        gateMatrix("rzz", {-2 * c}));
        REQUIRE(num::approxEqual(compiler::canonicalGate(a, b, c), want, 1e-13));
    }
}

TEST_CASE("KAK reconstructs random two-qubit unitaries exactly, phase included") {
    core::Random rng(21);
    for (int trial = 0; trial < 200; ++trial) {
        const num::Matrix u = num::scale(randomUnitary(rng), std::polar(1.0, angle(rng)));
        auto k = compiler::kakDecompose(u.view());
        INFO((k ? std::string() : k.error().format()));
        REQUIRE(k.has_value());
        REQUIRE(num::approxEqual(k->reconstruct(), u, 1e-9));
        for (double v : {k->a, k->b, k->c}) {
            CHECK(v > -kPi / 4 - 1e-12);
            CHECK(v <= kPi / 4 + 1e-12);
        }
        for (const num::Matrix* m : {&k->before0, &k->before1, &k->after0, &k->after1}) {
            REQUIRE(num::isUnitary(m->view(), 1e-9));
            CHECK(std::abs((*m)(0, 0) * (*m)(1, 1) - (*m)(0, 1) * (*m)(1, 0) - 1.0) <
                  1e-9); // SU(2)
        }
    }
    auto bad = compiler::kakDecompose(num::Matrix::identity(2).view());
    CHECK_FALSE(bad.has_value());
}

TEST_CASE("canonical coordinates and minimal cx counts of the library gates (T02 §6)") {
    struct Row {
        std::string name;
        std::vector<double> params;
        std::array<double, 3> coords;
        std::uint32_t cx;
    };
    const double q = kPi / 4;
    const std::vector<Row> table = {
        {"cx", {}, {q, 0, 0}, 1},
        {"cz", {}, {q, 0, 0}, 1},
        {"ecr", {}, {q, 0, 0}, 1},
        {"ch", {}, {q, 0, 0}, 1},
        {"iswap", {}, {q, q, 0}, 2},
        {"siswap", {}, {q / 2, q / 2, 0}, 2},
        {"swap", {}, {q, q, q}, 3},
        {"rzz", {0.6}, {0.3, 0, 0}, 2},
        {"cp", {1.0}, {0.25, 0, 0}, 2},
        {"rxx", {kPi / 2}, {q, 0, 0}, 1},
        {"fsim", {0.4, 0.0}, {0.2, 0.2, 0}, 2},
    };
    for (const Row& r : table) {
        auto k = compiler::kakDecompose(gateMatrix(r.name, r.params).view());
        REQUIRE(k.has_value());
        const auto got = sorted(*k);
        INFO(r.name << ": " << got[0] << ", " << got[1] << ", " << got[2]);
        for (std::size_t i = 0; i < 3; ++i)
            CHECK(std::abs(got[i] - r.coords[i]) < 1e-9);
        CHECK(k->cxCount() == r.cx);
    }
    // A local gate has all coordinates zero.
    auto local = compiler::kakDecompose(num::kron(gateMatrix("h"), gateMatrix("t")).view());
    REQUIRE(local.has_value());
    CHECK(local->cxCount() == 0);
}

TEST_CASE("two-qubit synthesis over {U, cx} uses the minimal cx count and matches the unitary") {
    core::Random rng(8);
    std::vector<std::pair<num::Matrix, std::uint32_t>> cases;
    for (int trial = 0; trial < 60; ++trial)
        cases.emplace_back(randomUnitary(rng), 3u);
    for (const char* name : {"cx", "cz", "ecr", "cy"})
        cases.emplace_back(gateMatrix(name), 1u);
    cases.emplace_back(gateMatrix("iswap"), 2u);
    cases.emplace_back(gateMatrix("siswap"), 2u);
    cases.emplace_back(gateMatrix("swap"), 3u);
    cases.emplace_back(gateMatrix("crz", {0.8}), 2u);
    cases.emplace_back(gateMatrix("ryy", {0.8}), 2u);
    cases.emplace_back(gateMatrix("rxx", {-kPi / 2}), 1u);
    cases.emplace_back(num::kron(gateMatrix("sx"), gateMatrix("ry", {0.3})), 0u);
    for (const auto& [u, cx] : cases) {
        auto seq = compiler::synthesizeTwoQubit(u.view(), W(0), W(1));
        REQUIRE(seq.has_value());
        const ir::Circuit c = circuit(2, *seq);
        CHECK(countGates(c, "cx") == cx);
        for (const ir::Gate* g : gatesOf(c))
            CHECK((g->name == "U" || g->name == "cx"));
        CHECK(gatesOf(c).size() <= 2 * (cx + 1) + cx); // at most one U per wire between entanglers
        REQUIRE(num::equalUpToGlobalPhase(u, unitary(c), 1e-9));
    }
    // An explicit 4×4 matrix is decomposable to every native set (it was QL4070 without §5.5).
    auto custom = ir::makeUnitary(randomUnitary(rng), {W(1), W(0)});
    REQUIRE(custom.has_value());
    for (const auto& target :
         {compiler::Target::universal(), targetOf("sc_fixed_5", "ecr"), targetOf("ion_chain_11")}) {
        ir::Circuit c = circuit(2, {*custom});
        const num::Matrix want = unitary(c);
        REQUIRE(compiler::decompose(c, target).has_value());
        for (const ir::Gate* g : gatesOf(c))
            REQUIRE(target.accepts(*g));
        REQUIRE(num::equalUpToGlobalPhase(want, unitary(c, 2), 1e-9));
    }
}

TEST_CASE("block resynthesis lowers the two-qubit count and keeps the unitary") {
    core::Random rng(14);
    const auto target = targetOf("sc_fixed_5");
    // Five cx on one pair with local gates between them, next to an untouched pair. Generic local
    // gates need three cx; with rz on q0 and sx on q1 the five cx collapse to a single one
    // (rz ⊗ sx commutes with cx q0,q1, which leaves swap · local · swap · cx q1,q0). The expected
    // count is the oracle of T02 §6: the cx count of the block's canonical coordinates.
    for (bool generic : {true, false}) {
        std::vector<ir::Gate> gs;
        for (int k = 0; k < 5; ++k) {
            gs.push_back(G("cx", {k % 2 ? 1u : 0u, k % 2 ? 0u : 1u}));
            gs.push_back(generic ? G("U", {0}, {angle(rng), angle(rng), angle(rng)})
                                 : G("rz", {0}, {angle(rng)}));
            gs.push_back(generic ? G("U", {1}, {angle(rng), angle(rng), angle(rng)})
                                 : G("sx", {1}));
        }
        const std::uint32_t minimal =
            compiler::kakDecompose(unitary(circuit(2, gs)).view())->cxCount();
        CHECK(minimal == (generic ? 3u : 1u));
        gs.push_back(G("cx", {2, 3}));
        gs.push_back(G("cx", {1, 2})); // closes the block
        gs.push_back(G("cx", {0, 1}));
        const ir::Circuit source = circuit(4, gs);
        ir::Circuit c = source;
        auto stats = compiler::resynthesizeTwoQubitBlocks(c, target);
        REQUIRE(stats.has_value());
        CHECK(stats->blocks == 1);
        CHECK(stats->replaced == 1);
        CHECK(stats->twoQubitBefore == 5);
        CHECK(stats->twoQubitAfter == minimal);
        CHECK(c.twoQubitCount() == source.twoQubitCount() - 5 + minimal);
        for (const ir::Gate* g : gatesOf(c))
            CHECK(target.accepts(*g));
        REQUIRE(num::equalUpToGlobalPhase(unitary(source), unitary(c, 4), 1e-9));
    }
    // A block that is already minimal stays as it is.
    ir::Circuit minimal = circuit(2, {G("cx", {0, 1}), G("rz", {1}, {0.3}), G("cx", {0, 1})});
    const ir::Circuit before = minimal;
    REQUIRE(compiler::resynthesizeTwoQubitBlocks(minimal, target).has_value());
    CHECK(minimal.structurallyEqual(before));

    // Through the pipeline: level 2 (or the kak flag) never uses more two-qubit gates than level 1.
    std::string body = "qubit[3] q;\n";
    for (int k = 0; k < 12; ++k)
        body += std::string(k % 3 == 0   ? "cz"
                            : k % 3 == 1 ? "cx"
                                         : "swap") +
                " q[0], q[1];\nry(0." + std::to_string(k + 1) + ") q[1];\nh q[0];\n";
    body += "cx q[1], q[2];\n";
    const auto prog = parse(program(body));
    const auto& d = device("sc_heavyhex_27");
    compiler::CompileOptions plain, kak;
    kak.kak = true;
    auto a = compiler::compile(prog, d.device, d.calibration, plain),
         b = compiler::compile(prog, d.device, d.calibration, kak);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(b->metrics.twoQubitCount <= 4); // one block of 3 cx + the last cx
    CHECK(b->metrics.twoQubitCount < a->metrics.twoQubitCount);
    REQUIRE(b->equivalence.has_value());
    CHECK(b->equivalence->equivalent);
}
