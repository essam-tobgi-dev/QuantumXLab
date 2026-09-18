// Spec 14 §5, 25 §4 — randomized oracles: on random native circuits the optimizer preserves the
// unitary (1e-9), never increases the gate or pulse count, is idempotent, and virtual-Z
// bookkeeping records the accumulated frame phase.
#include "CompilerTestUtil.hpp"
#include <cmath>

using namespace ctest;
using compiler::Basis1q;

namespace {
// Random circuit over a native set; small angles sets are mixed in so that cancellations occur.
ir::Circuit randomNative(core::Random& rng, Basis1q basis, std::uint32_t n, std::size_t length) {
    std::vector<ir::Gate> gs;
    const double special[] = {kPi / 2, -kPi / 2, kPi, kPi / 4, 0.3, -0.3};
    auto theta = [&] { return rng.bernoulli(0.5) ? special[rng.uniformInt(6)] : angle(rng); };
    for (std::size_t k = 0; k < length; ++k) {
        const auto a = static_cast<std::uint32_t>(rng.uniformInt(n));
        auto b = static_cast<std::uint32_t>(rng.uniformInt(n - 1));
        if (b >= a) ++b;
        const auto pick = rng.uniformInt(10);
        switch (basis) {
        case Basis1q::ZSX:
            if (pick < 3) gs.push_back(G("rz", {a}, {theta()}));
            else if (pick < 5) gs.push_back(G("sx", {a}));
            else if (pick < 6) gs.push_back(G("x", {a}));
            else if (pick < 9) gs.push_back(G("cx", {a, b}));
            else gs.push_back(G("cz", {a, b}));
            break;
        case Basis1q::ZYZ:
            if (pick < 3) gs.push_back(G("rz", {a}, {theta()}));
            else if (pick < 5) gs.push_back(G("rx", {a}, {theta()}));
            else if (pick < 7) gs.push_back(G("ry", {a}, {theta()}));
            else gs.push_back(G("ms", {a, b}, {theta() / 2}));
            break;
        case Basis1q::U:
            if (pick < 6) gs.push_back(G("U", {a}, {theta(), theta(), theta()}));
            else gs.push_back(G("cx", {a, b}));
            break;
        }
    }
    return circuit(n, std::move(gs));
}
std::size_t pulses(const ir::Circuit& c) {
    std::size_t p = 0;
    for (const auto* g : gatesOf(c))
        if (g->targets.size() == 1 && !compiler::isFrameChange(g->name)) ++p;
    return p;
}
} // namespace

TEST_CASE("random native circuits: unitary preserved, counts never grow, second run is a no-op") {
    core::Random rng(1234);
    std::size_t totalBefore = 0, totalAfter = 0;
    for (Basis1q basis : {Basis1q::ZSX, Basis1q::ZYZ, Basis1q::U})
        for (int trial = 0; trial < 25; ++trial) {
            const ir::Circuit source = randomNative(rng, basis, 4, 80);
            ir::Circuit c = source;
            auto stats = compiler::optimize(c, basis);
            REQUIRE(stats.has_value());
            INFO("basis " << static_cast<int>(basis) << " trial " << trial << "\n" << ir::dump(source) << "->\n" << ir::dump(c));
            REQUIRE(num::equalUpToGlobalPhase(unitary(source), unitary(c, 4), 1e-9));
            // Cost order of §5.2: pulses first, then gates (a saved pulse may cost frame changes).
            CHECK(pulses(c) <= pulses(source));
            CHECK((pulses(c) < pulses(source) || gatesOf(c).size() <= gatesOf(source).size()));
            CHECK(c.twoQubitCount() <= source.twoQubitCount());
            CHECK(stats->gatesBefore == gatesOf(source).size());
            CHECK(stats->gatesAfter == gatesOf(c).size());
            CHECK(stats->iterations < 50);                       // a fixpoint, not the iteration bound
            totalBefore += gatesOf(source).size();
            totalAfter += gatesOf(c).size();
            // Spec 25 §4: running the optimizer twice yields the same IR.
            ir::Circuit again = c;
            REQUIRE(compiler::optimize(again, basis).has_value());
            REQUIRE(again.structurallyEqual(c, 1e-12));
        }
    // Random circuits of this density lose a sizeable fraction of their gates.
    CHECK(totalAfter * 10 < totalBefore * 9);
}

TEST_CASE("optimizing a decomposed circuit keeps it native and equivalent to the source") {
    core::Random rng(77);
    const char* names1[] = {"h", "t", "s", "x", "y", "sx", "tdg"};
    for (const auto& [label, target] : std::vector<std::pair<std::string, compiler::Target>>{
             {"{U,cx}", compiler::Target::universal()}, {"sc cx", targetOf("sc_fixed_5")}, {"sc ecr", targetOf("sc_fixed_5", "ecr")},
             {"grid cz", targetOf("sc_tunable_grid_54")}, {"ion", targetOf("ion_chain_11")}})
        for (int trial = 0; trial < 6; ++trial) {
            std::vector<ir::Gate> gs;
            for (int k = 0; k < 30; ++k) {
                const auto a = static_cast<std::uint32_t>(rng.uniformInt(4));
                auto b = static_cast<std::uint32_t>(rng.uniformInt(3));
                if (b >= a) ++b;
                switch (rng.uniformInt(6)) {
                case 0: gs.push_back(G("cx", {a, b})); break;
                case 1: gs.push_back(G("cp", {a, b}, {angle(rng)})); break;
                case 2: gs.push_back(G("swap", {a, b})); break;
                case 3: gs.push_back(G("ry", {a}, {angle(rng)})); break;
                default: gs.push_back(G(names1[rng.uniformInt(7)], {a})); break;
                }
            }
            const ir::Circuit source = circuit(4, std::move(gs));
            ir::Circuit c = source;
            REQUIRE(compiler::decompose(c, target).has_value());
            const std::size_t lowered = gatesOf(c).size();
            REQUIRE(compiler::optimize(c, target).has_value());
            INFO(label << " trial " << trial);
            for (const auto* g : gatesOf(c)) REQUIRE(target.accepts(*g));
            REQUIRE(num::equalUpToGlobalPhase(unitary(source), unitary(c, 4), 1e-8));
            CHECK(gatesOf(c).size() < lowered);
        }
}

TEST_CASE("§5.4 virtual Z: rz gates become zero-duration frame changes and the frame phase is recorded") {
    ir::Circuit c = circuit(2, {G("rz", {0}, {0.3}), G("sx", {0}), G("rz", {0}, {0.5}), G("cx", {0, 1}), G("rz", {1}, {-kPi}), G("x", {1})});
    auto info = compiler::virtualZ(c);
    REQUIRE(info.has_value());
    CHECK(info->frameChanges == 3);
    CHECK(std::abs(info->finalPhase[0] - 0.8) < 1e-12);
    CHECK(std::abs(info->finalPhase[1] - kPi) < 1e-12);           // reduced to (−π, π]
    for (const auto* g : gatesOf(c)) {
        if (g->name == "rz") { REQUIRE(g->duration.has_value()); CHECK(g->duration->get() == 0); }
        else CHECK_FALSE(g->duration.has_value());
    }
    const auto& vz = c.meta()["virtual_z"];
    CHECK(vz["frame_changes"] == 3);
    // Parallel arrays (node index, wire, phase): sx sees 0.3, cx sees 0.8 on its control, x sees π.
    CHECK(vz["phases"]["node"].get<std::vector<std::uint32_t>>() == std::vector<std::uint32_t>{1, 3, 5});
    CHECK(vz["phases"]["wire"].get<std::vector<std::uint32_t>>() == std::vector<std::uint32_t>{0, 0, 1});
    const auto phase = vz["phases"]["phase"].get<std::vector<double>>();
    REQUIRE(phase.size() == 3);
    CHECK(std::abs(phase[0] - 0.3) < 1e-12);
    CHECK(std::abs(phase[1] - 0.8) < 1e-12);
    CHECK(std::abs(phase[2] - kPi) < 1e-12);
    CHECK(std::abs(vz["final_phase"]["0"].get<double>() - 0.8) < 1e-12);
}
