// Spec 14 §4.2–§4.3, 25 §4 — every decomposition rule equals its source gate EXACTLY (phase
// included) at 50 random parameter sets, and single-qubit synthesis is exact in every basis.
#include "CompilerTestUtil.hpp"
#include <cmath>
#include <complex>
#include <set>

using namespace ctest;
using compiler::Basis1q;

namespace {
// Product of a synthesised sequence (time order) as a 2×2 matrix.
num::Matrix productOf(const compiler::OneQubitSequence& s) {
    num::Matrix m = num::Matrix::identity(2);
    for (const auto& g : s.gates) {
        auto u = ir::gates::matrix(g.name, g.params);
        REQUIRE(u.has_value());
        m = num::matmul(*u, m);
    }
    return num::scale(m, std::polar(1.0, s.phase));
}
std::size_t countNamed(const compiler::OneQubitSequence& s, std::string_view name) {
    std::size_t n = 0;
    for (const auto& g : s.gates)
        if (g.name == name)
            ++n;
    return n;
}
} // namespace

TEST_CASE(
    "every rule of the table reproduces its source gate exactly at 50 random parameter sets") {
    core::Random rng(20260917);
    std::set<std::string> sources;
    for (const compiler::Rule& rule : compiler::decompositionRules()) {
        const ir::GateDef* def = ir::gates::find(rule.source);
        REQUIRE(def != nullptr);
        sources.insert(std::string(rule.source));
        const auto n = static_cast<std::uint32_t>(def->nQubits);
        for (int trial = 0; trial < 50; ++trial) {
            std::vector<double> params;
            for (int k = 0; k < def->nParams; ++k)
                params.push_back(angle(rng));
            std::vector<std::uint32_t> wires;
            for (std::uint32_t q = 0; q < n; ++q)
                wires.push_back(q);
            const ir::Gate source = G(rule.source, wires, params);
            auto expansion = compiler::applyRule(rule, source);
            REQUIRE(expansion.has_value());
            const num::Matrix want = unitary(circuit(n, {source}));
            const num::Matrix got = num::scale(unitary(circuit(n, expansion->gates)),
                                               std::polar(1.0, expansion->phase));
            INFO("rule " << rule.source << " [" << rule.family << "] trial " << trial);
            REQUIRE(num::approxEqual(want, got, 1e-12));
        }
    }
    // Spec 14 §4.3 table: every multi-qubit library gate other than cx has a generic rule.
    for (const ir::GateDef& d : ir::gates::all()) {
        if (d.nQubits < 2 || d.name == "cx")
            continue;
        INFO("gate " << d.name);
        CHECK(compiler::findGenericRule(d.name) != nullptr);
    }
    for (const char* family : {"cz", "ecr", "rxx", "siswap"}) {
        const compiler::Rule* r = compiler::findRule("cx", family);
        REQUIRE(r != nullptr);
        CHECK(r->family == family);
    }
    CHECK(compiler::findRule("cx", "cx") ==
          nullptr); // cx is the generic entangler: nothing to rewrite
}

TEST_CASE("rule table serialises as the decompositions.json document") {
    const core::Json j = core::Json::parse(compiler::decompositionRulesJson());
    REQUIRE(j["qxl"]["kind"] == "theory.decompositions");
    const auto& rules = j["data"]["rules"];
    REQUIRE(rules.size() == compiler::decompositionRules().size());
    bool sawCp = false;
    for (const auto& r : rules) {
        if (r["source"] != "cp" || r["target_set"] != "1q+cx")
            continue;
        sawCp = true;
        REQUIRE(r["sequence"].size() == 5);
        CHECK(r["sequence"][0]["gate"] == "p");
        CHECK(r["sequence"][0]["params"][0] == "0.5*p0");
        CHECK(r["sequence"][2]["params"][0] == "-0.5*p0");
        CHECK(r["phase"] == "0");
    }
    CHECK(sawCp);
    CHECK(compiler::Affine{-3 * kPi / 4, {}}.text() == "-3*pi/4");
    CHECK(compiler::Affine{0, {0, 0.5, 0.5, 1}}.text() == "0.5*p1 + 0.5*p2 + p3");
}

TEST_CASE("euler angles: U(θ,φ,λ) and the phase are recovered from the matrix") {
    core::Random rng(7);
    for (int trial = 0; trial < 200; ++trial) {
        const double theta = rng.uniform(0.01, kPi - 0.01), phi = angle(rng), lambda = angle(rng),
                     gamma = angle(rng);
        const num::Matrix m = num::scale(ir::gates::u3(theta, phi, lambda), std::polar(1.0, gamma));
        const auto e = compiler::eulerAngles(m.view());
        CHECK(std::abs(e.theta - theta) < 1e-12);
        const num::Matrix back =
            num::scale(ir::gates::u3(e.theta, e.phi, e.lambda), std::polar(1.0, e.phase));
        REQUIRE(num::approxEqual(m, back, 1e-13));
    }
    // Degenerate cases (spec 14 §5.2): φ = 0 for a diagonal matrix, λ = 0 for an anti-diagonal one.
    const auto d = compiler::eulerAngles(ir::gates::matrix("rz", std::vector<double>{0.7})->view());
    CHECK(d.theta == 0.0);
    CHECK(d.phi == 0.0);
    CHECK(std::abs(d.lambda - 0.7) < 1e-15);
    CHECK(std::abs(d.phase + 0.35) < 1e-15);
    const auto a = compiler::eulerAngles(ir::gates::matrix("y", {})->view());
    CHECK(std::abs(a.theta - kPi) < 1e-15);
    CHECK(a.lambda == 0.0);
}

TEST_CASE(
    "single-qubit synthesis is exact in every basis for random unitaries and all library gates") {
    core::Random rng(11);
    std::vector<num::Matrix> cases;
    for (int trial = 0; trial < 300; ++trial)
        cases.push_back(num::scale(ir::gates::u3(angle(rng), angle(rng), angle(rng)),
                                   std::polar(1.0, angle(rng))));
    for (const ir::GateDef& d : ir::gates::all()) {
        if (d.nQubits != 1 || d.name == "unitary")
            continue;
        std::vector<double> params;
        for (int k = 0; k < d.nParams; ++k)
            params.push_back(angle(rng));
        cases.push_back(*ir::gates::matrix(d.name, params));
    }
    for (double special : {0.0, kPi / 2, -kPi / 2, kPi, -kPi, 2 * kPi, 3 * kPi / 2})
        for (double phi : {0.0, 0.3, -kPi / 2, kPi})
            cases.push_back(ir::gates::u3(special, phi, 0.9 - phi));
    for (const auto& m : cases)
        for (Basis1q basis : {Basis1q::U, Basis1q::ZSX, Basis1q::ZYZ}) {
            const auto seq = compiler::synthesize1q(m.view(), basis);
            REQUIRE(num::approxEqual(m, productOf(seq), 1e-12));
            for (const auto& g : seq.gates) {
                if (basis == Basis1q::U)
                    CHECK(g.name == "U");
                if (basis == Basis1q::ZSX)
                    CHECK((g.name == "rz" || g.name == "sx" || g.name == "x"));
                if (basis == Basis1q::ZYZ)
                    CHECK((g.name == "rz" || g.name == "ry" || g.name == "rx"));
                if (g.name == "rz")
                    CHECK(std::abs(g.params[0]) <= kPi + 1e-15); // angles are reduced to (−π, π]
            }
            CHECK(seq.gates.size() <= (basis == Basis1q::U ? 1u : basis == Basis1q::ZSX ? 5u : 3u));
        }
}

TEST_CASE("spec 14 §4.2 simplifications: pulse counts of the special angles") {
    auto zsx = [](std::string_view name, std::vector<double> p = {}) {
        return compiler::synthesize1q(ir::gates::matrix(name, p)->view(), Basis1q::ZSX);
    };
    // θ = 0: a frame change only.
    CHECK(zsx("rz", {0.4}).pulses() == 0);
    CHECK(zsx("rz", {0.4}).gates.size() == 1);
    CHECK(zsx("t").gates.size() == 1);
    CHECK(zsx("id").gates.empty());
    CHECK(zsx("rz", {2 * kPi}).gates.empty()); // Rz(2π) = −I
    // θ = π/2: one sx.
    CHECK(countNamed(zsx("h"), "sx") == 1);
    CHECK(zsx("h").gates.size() == 3); // rz(π/2) · sx · rz(π/2)
    CHECK(zsx("sx").gates.size() == 1);
    CHECK(countNamed(zsx("rx", {-kPi / 2}), "sx") == 1);
    // θ = π: one x, plain when φ − λ ≡ π (mod 2π).
    CHECK(zsx("x").gates.size() == 1);
    CHECK(zsx("x").gates[0].name == "x");
    CHECK(countNamed(zsx("y"), "x") == 1);
    CHECK(zsx("y").gates.size() == 2);
    // General θ: two sx and three frame changes.
    const auto general = zsx("ry", {0.3});
    CHECK(countNamed(general, "sx") == 2);
    CHECK(general.pulses() == 2);
    // Ions: rx and ry stay one native rotation, a π flip drops one rz.
    auto zyz = [](std::string_view name, std::vector<double> p = {}) {
        return compiler::synthesize1q(ir::gates::matrix(name, p)->view(), Basis1q::ZYZ);
    };
    CHECK(zyz("rx", {0.8}).gates.size() == 1);
    CHECK(zyz("rx", {0.8}).gates[0].name == "rx");
    CHECK(zyz("ry", {0.8}).gates.size() == 1);
    CHECK(zyz("h").gates.size() == 2);
    CHECK(zyz("x").gates.size() == 1); // X = i·Rx(π)
    CHECK(zyz("x").gates[0].name == "rx");
    CHECK(zyz("y").gates.size() == 1); // Y = i·Ry(π)
    CHECK(zyz("y").gates[0].name == "ry");
    CHECK(zyz("z").gates.size() == 1);
    CHECK(zyz("s").gates.size() == 1);
}
