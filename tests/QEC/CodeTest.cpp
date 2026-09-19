// Spec 16 §1–§2, §9; spec 25 §3.8 — every shipped code passes the load-time validation
// (commutation, independence, logical-operator relations, exhaustive distance), the generated
// families reproduce the assets, and each defect is reported by name.
#include "QEC/Code.hpp"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <set>

using namespace qlab;
using namespace qlab::qec;

namespace {
StabilizerCode shipped(const std::string& id) {
    auto c = loadShippedCode(id);
    if (!c)
        FAIL("loading " << id << ": " << c.error().format());
    return *c;
}
PauliString P(std::string_view s) {
    return PauliString::parse(s).value();
}
} // namespace

TEST_CASE("Codes: every shipped code validates and has its declared parameters") {
    // [[n, k, d]] of spec 16 §2.
    const std::map<std::string, std::array<std::uint32_t, 3>> expected = {
        {"repetition_bitflip_3", {3, 1, 3}},
        {"repetition_phaseflip_3", {3, 1, 3}},
        {"shor_9", {9, 1, 3}},
        {"steane_7", {7, 1, 3}},
        {"five_qubit", {5, 1, 3}},
        {"surface_rot_3", {9, 1, 3}},
        {"surface_rot_5", {25, 1, 5}},
        {"surface_rot_7", {49, 1, 7}}};
    REQUIRE(shippedCodeIds().size() == expected.size());
    for (const std::string& id : shippedCodeIds()) {
        INFO("code " << id);
        const StabilizerCode c = shipped(id);
        const auto nkd = expected.at(id);
        REQUIRE(c.id == id);
        REQUIRE(c.n == nkd[0]);
        REQUIRE(c.k == nkd[1]);
        REQUIRE(c.d == nkd[2]);
        REQUIRE(c.stabilizers.size() == c.n - c.k);
        REQUIRE(c.ancillas.size() == c.stabilizers.size());
        REQUIRE(c.dataLayout.size() == c.n);
        // The relations themselves, independently of verifyCode (spec 25 §3.8 "Stabilizer sets").
        for (const auto& g : c.stabilizers) {
            for (const auto& h : c.stabilizers)
                REQUIRE(g.commutesWith(h));
            REQUIRE(g.commutesWith(c.logicalX[0]));
            REQUIRE(g.commutesWith(c.logicalZ[0]));
        }
        REQUIRE_FALSE(c.logicalX[0].commutesWith(c.logicalZ[0]));
        REQUIRE(symplecticRank(c.stabilizers) == c.n - c.k);
    }
}

TEST_CASE("Codes: exhaustive distances (T09 (2.1))") {
    // Quantum distance, X-only and Z-only distances, and the figure compared with the file's "d".
    struct Row {
        const char* id;
        std::uint32_t distance, dX, dZ, declared;
    };
    const Row rows[] = {{"repetition_bitflip_3", 1, 3, 1, 3},
                        {"repetition_phaseflip_3", 1, 1, 3, 3},
                        {"shor_9", 3, 3, 3, 3},
                        {"steane_7", 3, 3, 3, 3},
                        {"surface_rot_3", 3, 3, 3, 3},
                        {"surface_rot_5", 5, 5, 5, 5}};
    for (const Row& r : rows) {
        INFO("code " << r.id);
        const StabilizerCode c = shipped(r.id);
        const auto rep = computeDistance(c);
        REQUIRE(rep.has_value());
        REQUIRE(rep->distance == r.distance);
        REQUIRE(rep->dX == r.dX);
        REQUIRE(rep->dZ == r.dZ);
        REQUIRE(rep->declared == r.declared);
        // The witness is a logical operator of that weight: it commutes with every generator and is
        // not a product of generators.
        REQUIRE(rep->witness.weight() == r.declared);
        for (const auto& g : c.stabilizers)
            REQUIRE(g.commutesWith(rep->witness));
        REQUIRE_FALSE(inGroup(c.stabilizers, rep->witness));
    }
    // The five-qubit code is not CSS: the search runs over all 3^w letter assignments.
    const StabilizerCode five = shipped("five_qubit");
    REQUIRE_FALSE(five.isCss());
    const auto rep = computeDistance(five);
    REQUIRE(rep.has_value());
    REQUIRE(rep->distance == 3);
    REQUIRE(rep->declared == 3);
    // Weight-3 logical operators of the five-qubit code exist, weight-2 ones do not: the witness
    // has weight exactly 3 and no two-letter string commutes with all four generators (perfect
    // code: the 15 single-qubit errors exhaust the 15 non-zero syndromes, T09 §4.5).
    REQUIRE(rep->witness.weight() == 3);
    std::set<std::vector<std::uint8_t>> syndromes;
    for (std::uint32_t q = 0; q < 5; ++q)
        for (char l : {'X', 'Y', 'Z'})
            syndromes.insert(syndromeOf(five.stabilizers, PauliString::single(5, q, l)));
    REQUIRE(syndromes.size() == 15);
}

TEST_CASE("Codes: structure predicates") {
    REQUIRE(shipped("steane_7").isCss());
    REQUIRE_FALSE(shipped("steane_7").isMatchable()); // qubit 6 sits in three checks of each type
    REQUIRE_FALSE(shipped("five_qubit").isMatchable());
    for (const char* id : {"repetition_bitflip_3", "repetition_phaseflip_3", "shor_9",
                           "surface_rot_3", "surface_rot_5"})
        REQUIRE(shipped(id).isMatchable());
    const StabilizerCode five = shipped("five_qubit");
    REQUIRE(five.checkType(0) == CheckType::Mixed);
    REQUIRE(five.ancillas[0].type == CheckType::Mixed);
    const StabilizerCode s3 = shipped("surface_rot_3");
    REQUIRE(s3.checkType(0) == CheckType::Z);
    REQUIRE(s3.checkType(1) == CheckType::X);
    REQUIRE(s3.logical(LogicalBasis::Z).str() == "IIZIIZIIZ");
    REQUIRE(s3.logical(LogicalBasis::X).str() == "XXXIIIIII");
    REQUIRE(s3.defaultDecoder == "lookup");
    REQUIRE(shipped("surface_rot_5").defaultDecoder == "union_find");
}

TEST_CASE("Codes: generated families reproduce the shipped assets") {
    auto same = [](const StabilizerCode& a, const StabilizerCode& b) {
        REQUIRE(a.id == b.id);
        REQUIRE(a.n == b.n);
        REQUIRE(a.k == b.k);
        REQUIRE(a.d == b.d);
        REQUIRE(a.family == b.family);
        REQUIRE(a.stabilizers == b.stabilizers);
        REQUIRE(a.logicalX == b.logicalX);
        REQUIRE(a.logicalZ == b.logicalZ);
        REQUIRE(a.dataLayout == b.dataLayout);
        REQUIRE(a.ancillas.size() == b.ancillas.size());
        for (std::size_t j = 0; j < a.ancillas.size(); ++j) {
            REQUIRE(a.ancillas[j].type == b.ancillas[j].type);
            REQUIRE(a.ancillas[j].coord == b.ancillas[j].coord);
            REQUIRE(a.ancillas[j].order == b.ancillas[j].order);
        }
        REQUIRE(a.defaultDecoder == b.defaultDecoder);
    };
    for (std::uint32_t d : {3u, 5u, 7u})
        same(makeRotatedSurfaceCode(d).value(), shipped("surface_rot_" + std::to_string(d)));
    same(makeRepetitionCode(3, false).value(), shipped("repetition_bitflip_3"));
    same(makeRepetitionCode(3, true).value(), shipped("repetition_phaseflip_3"));
    // Spec 16 §2: repetition_bitflip_d for d ∈ {3, 5, …, 21}; spec 16 §2.1: 2d² − 1 qubits in
    // total.
    for (std::uint32_t d = 3; d <= 21; d += 2) {
        const StabilizerCode rep = makeRepetitionCode(d).value();
        REQUIRE(verifyCode(rep).has_value());
        REQUIRE(rep.stabilizers.size() == d - 1);
    }
    for (std::uint32_t d : {3u, 5u, 7u, 9u, 11u}) {
        const StabilizerCode s = makeRotatedSurfaceCode(d).value();
        REQUIRE(verifyCode(s).has_value());
        REQUIRE(s.n + s.ancillas.size() == 2 * d * d - 1);
    }
    REQUIRE(makeRotatedSurfaceCode(4).error().code == err::BadOptions);
    REQUIRE(makeRepetitionCode(1).error().code == err::BadOptions);
}

TEST_CASE("Codes: JSON round trip keeps the schema of Assets/QEC") {
    for (const std::string& id : shippedCodeIds()) {
        INFO("code " << id);
        const StabilizerCode c = shipped(id);
        const core::Json j = codeToJson(c);
        for (const char* key : {"id", "n", "k", "d", "family", "stabilizers", "logical_x",
                                "logical_z", "layout", "decoder", "theory"})
            REQUIRE(j.contains(key));
        const auto back = codeFromJson(j);
        REQUIRE(back.has_value());
        REQUIRE(back->stabilizers == c.stabilizers);
        REQUIRE(back->logicalX == c.logicalX);
        REQUIRE(back->logicalZ == c.logicalZ);
        REQUIRE(back->family == c.family);
        REQUIRE(back->theoryRefs == c.theoryRefs);
        REQUIRE(verifyCode(*back).has_value());
    }
    // Mixed checks carry their generator (asset field "pauli").
    REQUIRE(codeToJson(shipped("five_qubit"))["layout"]["ancilla"][2]["pauli"] == "XIXZZ");
}

TEST_CASE("Codes: each defect is a load error naming the offender") {
    const StabilizerCode good = shipped("steane_7");
    {
        StabilizerCode c = good;
        c.stabilizers[3] = P("XIIZZZZ"); // its X on qubit 0 anticommutes with generator 5, ZIZIZIZ
        const auto r = verifyCode(c);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == err::NotCommuting);
        REQUIRE(r.error().message.find("XIIZZZZ") != std::string::npos);
    }
    {
        StabilizerCode c = good;
        c.stabilizers[2] = c.stabilizers[0] * c.stabilizers[1]; // dependent generator
        const auto r = verifyCode(c);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == err::NotIndependent);
    }
    {
        StabilizerCode c = good;
        c.logicalZ[0] = P("ZIIIIII"); // anticommutes with the generator XIXIXIX
        const auto r = verifyCode(c);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == err::BadLogical);
        REQUIRE(r.error().message.find("logical_z[0]") != std::string::npos);
    }
    {
        StabilizerCode c = good;
        c.logicalX[0] = c.logicalZ[0]; // commutes with its partner
        REQUIRE(verifyCode(c).error().code == err::BadLogical);
    }
    {
        StabilizerCode c = good;
        c.d = 5;
        const auto r = verifyCode(c);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == err::BadDistance);
        REQUIRE(r.error().message.find("weight 3") != std::string::npos);
    }
    {
        StabilizerCode c = shipped("surface_rot_3");
        std::swap(c.ancillas[1].order[1], c.ancillas[1].order[2]); // X check run as NW, SW, NE, SE
        const auto r = verifyCode(c);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == err::BadLayout);
        REQUIRE(r.error().message.find("Tomita") != std::string::npos);
    }
    {
        core::Json j = codeToJson(good);
        j.erase("logical_x");
        const auto r = codeFromJson(j);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error().code == err::BadJson);
        REQUIRE(r.error().message.find("data.logical_x") != std::string::npos);
        core::Json k = codeToJson(good);
        k["layout"]["ancilla"][4].erase("order");
        REQUIRE(codeFromJson(k).error().message.find("data.layout.ancilla[4].order") !=
                std::string::npos);
    }
    REQUIRE_FALSE(loadShippedCode("no_such_code").has_value());
}
