// Spec 16 §5, §9; spec 25 §3.8 — decoders under code-capacity errors. Oracle: the residual
// (error · correction) must lie in the stabilizer group — zero syndrome and no logical action —
// for every error of weight ≤ ⌊(d − 1)/2⌋ (T09 §2.1).
#include "QEC/Decoder.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <set>

using namespace qlab;
using namespace qlab::qec;

namespace {
StabilizerCode shipped(const std::string& id) {
    return loadShippedCode(id).value();
}

// True when decoding the syndrome of `error` returns the state to the code space unchanged: the
// residual has zero syndrome and commutes with every logical operator, i.e. it is a stabilizer.
bool corrects(IDecoder& decoder, const StabilizerCode& code, const PauliString& error) {
    const auto syndrome = syndromeOf(code.stabilizers, error);
    const auto correction = decoder.decode(SyndromeLattice::fromSyndrome(syndrome));
    REQUIRE(correction.has_value());
    const PauliString residual = error * correction->pauli;
    for (const auto& g : code.stabilizers)
        if (!g.commutesWith(residual))
            return false;
    for (std::uint32_t l = 0; l < code.k; ++l)
        if (!residual.commutesWith(code.logicalX[l]) || !residual.commutesWith(code.logicalZ[l]))
            return false;
    return true;
}

std::vector<PauliString> errorsOfWeight(const StabilizerCode& code, std::uint32_t weight,
                                        std::string_view letters) {
    std::vector<PauliString> out;
    std::vector<std::uint32_t> support(weight);
    std::function<void(std::uint32_t, std::uint32_t, PauliString)> place =
        [&](std::uint32_t depth, std::uint32_t from, PauliString p) {
            if (depth == weight) {
                out.push_back(std::move(p));
                return;
            }
            for (std::uint32_t q = from; q < code.n; ++q)
                for (char l : letters) {
                    PauliString next = p;
                    next.setLetter(q, l);
                    place(depth + 1, q + 1, std::move(next));
                }
        };
    place(0, 0, PauliString::identity(code.n));
    return out;
}
} // namespace

TEST_CASE("Decoders: d = 3 surface code corrects every single-qubit X, Y, Z error (union-find and "
          "lookup)") {
    const StabilizerCode code = shipped("surface_rot_3");
    for (const char* name : {"union_find", "lookup"}) {
        auto decoder = makeCodeCapacityDecoder(name, code);
        REQUIRE(decoder.has_value());
        REQUIRE((*decoder)->name() == std::string_view(name));
        std::size_t tried = 0;
        for (std::uint32_t q = 0; q < code.n; ++q)
            for (char letter : {'X', 'Y', 'Z'}) {
                INFO(name << ": " << letter << " on data qubit " << q);
                REQUIRE(corrects(**decoder, code, PauliString::single(code.n, q, letter)));
                ++tried;
            }
        REQUIRE(tried == 27);
        // No error: no correction.
        const auto none =
            (*decoder)->decode(SyndromeLattice::fromSyndrome(std::vector<std::uint8_t>(8, 0)));
        REQUIRE(none.has_value());
        REQUIRE(none->pauli.isIdentity());
    }
}

TEST_CASE("Decoders: every shipped code corrects all single-qubit errors it is built against") {
    // Letters the code protects against: the repetition codes detect one Pauli type only (T09
    // §4.1).
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"repetition_bitflip_3", "X"}, {"repetition_phaseflip_3", "Z"}, {"shor_9", "XYZ"},
        {"steane_7", "XYZ"},           {"five_qubit", "XYZ"},           {"surface_rot_3", "XYZ"},
        {"surface_rot_5", "XYZ"},      {"surface_rot_7", "XYZ"}};
    for (const auto& [id, letters] : cases) {
        const StabilizerCode code = shipped(id);
        std::vector<std::string> names = {code.defaultDecoder};
        if (code.isMatchable())
            names = {"union_find"};
        if (code.n <= 25)
            names.push_back("lookup");
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        for (const std::string& name : names) {
            auto decoder = makeCodeCapacityDecoder(name, code);
            INFO(id << " with " << name);
            REQUIRE(decoder.has_value());
            for (const PauliString& e : errorsOfWeight(code, 1, letters)) {
                INFO("error " << e.str());
                REQUIRE(corrects(**decoder, code, e));
            }
        }
    }
    // Union-find needs a matching-type code (spec 16 §5 table).
    REQUIRE(makeCodeCapacityDecoder("union_find", shipped("steane_7")).error().code ==
            err::NotMatchable);
    REQUIRE(makeCodeCapacityDecoder("union_find", shipped("five_qubit")).error().code ==
            err::NotMatchable);
    REQUIRE(makeCodeCapacityDecoder("mwpm", shipped("surface_rot_3")).error().code ==
            ErrorCode::Unsupported);
    REQUIRE(LookupDecoder::create(shipped("surface_rot_7")).error().code ==
            err::TooLarge); // 24 generators per table
}

TEST_CASE("Decoders: all errors up to weight (d − 1)/2 are corrected at d = 5 and d = 7") {
    const StabilizerCode five = shipped("surface_rot_5");
    for (const char* name : {"union_find", "lookup"}) {
        auto decoder = makeCodeCapacityDecoder(name, five).value();
        std::size_t failures = 0, total = 0;
        for (const PauliString& e : errorsOfWeight(five, 2, "XYZ")) {
            failures += corrects(*decoder, five, e) ? 0 : 1;
            ++total;
        }
        INFO(name);
        REQUIRE(total == 300 * 9); // C(25, 2) supports × 3² letters
        REQUIRE(failures == 0);
    }
    const StabilizerCode seven = shipped("surface_rot_7");
    auto uf = makeCodeCapacityDecoder("union_find", seven).value();
    std::size_t failures = 0, total = 0;
    for (const char* letters : {"X", "Z"})
        for (const PauliString& e : errorsOfWeight(seven, 3, letters)) {
            failures += corrects(*uf, seven, e) ? 0 : 1;
            ++total;
        }
    REQUIRE(total == 2 * 18424); // C(49, 3) per letter
    REQUIRE(failures == 0);
}

TEST_CASE("Decoders: lookup tables hold minimum-weight corrections") {
    // Steane: the Z-check syndrome of X on qubit j is the binary representation of j + 1 (T09
    // §4.4), so the 8-entry table is the identity plus the seven single-qubit corrections.
    const StabilizerCode steane = shipped("steane_7");
    auto lookup = LookupDecoder::create(steane).value();
    REQUIRE(lookup.tableEntries() == 8 + 8); // one table per check type (CSS)
    REQUIRE(lookup.maxCorrectionWeight() == 1);
    for (std::uint32_t j = 0; j < 7; ++j) {
        const auto s = syndromeOf(steane.stabilizers, PauliString::single(7, j, 'X'));
        // Generators 3, 4, 5 are the Z rows of (4.2) with row 3 the most significant bit.
        REQUIRE(unsigned(s[3]) * 4 + unsigned(s[4]) * 2 + unsigned(s[5]) == j + 1);
        const auto c = lookup.decode(SyndromeLattice::fromSyndrome(s)).value();
        REQUIRE(c.pauli == PauliString::single(7, j, 'X'));
    }
    // Five-qubit code: perfect, 2⁴ = 1 + 15 syndromes ↔ identity + 15 single-qubit errors (T09
    // §4.5).
    const StabilizerCode five = shipped("five_qubit");
    auto joint = LookupDecoder::create(five).value();
    REQUIRE(joint.tableEntries() == 16);
    REQUIRE(joint.maxCorrectionWeight() == 1);
    std::set<std::string> corrections;
    for (std::uint32_t index = 0; index < 16; ++index) {
        std::vector<std::uint8_t> s = {std::uint8_t(index & 1), std::uint8_t((index >> 1) & 1),
                                       std::uint8_t((index >> 2) & 1), std::uint8_t(index >> 3)};
        corrections.insert(joint.decode(SyndromeLattice::fromSyndrome(s)).value().pauli.str());
    }
    REQUIRE(corrections.size() == 16);
    // Surface d = 5: 2¹² entries per check type, all reachable; the deepest needs a weight above 2.
    auto big = LookupDecoder::create(shipped("surface_rot_5")).value();
    REQUIRE(big.tableEntries() == 2 * 4096);
    REQUIRE(big.maxCorrectionWeight() > 2);
    // Shor: Z0, Z1, Z2 share a syndrome (degenerate, T09 §2.3); the table answers with one of them
    // and the residual is a stabilizer.
    const StabilizerCode shor = shipped("shor_9");
    auto shorTable = LookupDecoder::create(shor).value();
    const auto s1 = syndromeOf(shor.stabilizers, PauliString::single(9, 1, 'Z'));
    REQUIRE(s1 == syndromeOf(shor.stabilizers, PauliString::single(9, 0, 'Z')));
    REQUIRE(shorTable.decode(SyndromeLattice::fromSyndrome(s1)).value().pauli.weight() == 1);
    REQUIRE(lookup.decode(SyndromeLattice::fromSyndrome(std::vector<std::uint8_t>(3, 0)))
                .error()
                .code == err::BadSyndrome);
}

TEST_CASE("Decoders: union-find clusters, boundary matching and matched edges") {
    const StabilizerCode code = shipped("surface_rot_5");
    const DecodingGraph graph = buildCodeCapacityGraph(code).value();
    REQUIRE(graph.detectors == 24);
    // One edge per data qubit and error type: 25 X edges + 25 Z edges, minus boundary duplicates
    // (two data qubits of a weight-2 boundary check of the other type give the same boundary edge).
    std::size_t boundaryEdges = 0;
    for (const GraphEdge& e : graph.edges) {
        REQUIRE(e.weight == 1.0); // unit weights in the code-capacity setting (spec 16 §5.1)
        REQUIRE(e.correction.size() == 1);
        boundaryEdges += e.boundary() ? 1 : 0;
    }
    REQUIRE(boundaryEdges > 0);
    REQUIRE(graph.edges.size() <= 50);
    UnionFindDecoder uf(graph);
    // A bulk X error: both adjacent Z checks fire, the clusters meet after one round and are
    // matched to each other through the shared qubit.
    const PauliString bulk = PauliString::single(25, 12, 'X');
    const auto c =
        uf.decode(SyndromeLattice::fromSyndrome(syndromeOf(code.stabilizers, bulk))).value();
    REQUIRE(c.pauli == bulk);
    REQUIRE(c.edges.size() == 1);
    REQUIRE_FALSE(c.edges[0].b == kBoundary);
    REQUIRE(uf.lastGrowthRounds() == 1);
    // A corner error fires one check: the cluster grows two half-steps to the boundary.
    const PauliString corner = PauliString::single(25, 4, 'X'); // (1, 9): top-left corner
    const auto cc =
        uf.decode(SyndromeLattice::fromSyndrome(syndromeOf(code.stabilizers, corner))).value();
    REQUIRE(cc.edges.size() == 1);
    REQUIRE(cc.edges[0].b == kBoundary);
    REQUIRE(uf.lastGrowthRounds() == 2);
    REQUIRE(inGroup(code.stabilizers, corner * cc.pauli));
    // The decoder is reusable and a clone is independent.
    auto copy = uf.clone();
    REQUIRE(copy->decode(SyndromeLattice::fromSyndrome(syndromeOf(code.stabilizers, bulk)))
                .value()
                .pauli == bulk);
    REQUIRE(
        uf.decode(SyndromeLattice::fromSyndrome(std::vector<std::uint8_t>(7, 0))).error().code ==
        err::BadSyndrome);
}

TEST_CASE("Decoders: every correction reproduces its syndrome, whatever the error weight") {
    // Random depolarizing errors at 10 % on d = 5: far beyond the guaranteed weight, so logical
    // failures happen, but the residual must always be syndrome-free for every growth option.
    const StabilizerCode code = shipped("surface_rot_5");
    const DecodingGraph graph = buildCodeCapacityGraph(code).value();
    UnionFindOptions unweighted, coarse;
    unweighted.weighted = false;
    coarse.resolution = 1;
    UnionFindDecoder standard(graph), plain(graph, unweighted), units(graph, coarse);
    auto lookup = LookupDecoder::create(code).value();
    core::Random rng(0xBADC0DE);
    std::size_t logicalFailures = 0;
    for (int trial = 0; trial < 1500; ++trial) {
        PauliString error = PauliString::identity(code.n);
        for (std::uint32_t q = 0; q < code.n; ++q)
            if (rng.uniform() < 0.10)
                error.setLetter(q, "XYZ"[rng.uniformInt(3)]);
        const auto lattice = SyndromeLattice::fromSyndrome(syndromeOf(code.stabilizers, error));
        for (IDecoder* decoder :
             {static_cast<IDecoder*>(&standard), static_cast<IDecoder*>(&plain),
              static_cast<IDecoder*>(&units), static_cast<IDecoder*>(&lookup)}) {
            const auto c = decoder->decode(lattice);
            REQUIRE(c.has_value());
            const PauliString residual = error * c->pauli;
            for (const auto& g : code.stabilizers)
                REQUIRE(g.commutesWith(residual));
        }
        const PauliString residual = error * standard.decode(lattice).value().pauli;
        logicalFailures +=
            (residual.commutesWith(code.logicalX[0]) && residual.commutesWith(code.logicalZ[0]))
                ? 0
                : 1;
    }
    REQUIRE(logicalFailures > 0);   // 10 % depolarizing is not harmless …
    REQUIRE(logicalFailures < 750); // … but the decoder is far better than a coin
}

TEST_CASE("Decoders: events outside the decoding graph are rejected") {
    // Bit-flip noise never fires an X check, so those detectors have no edge: an event there cannot
    // be explained and the decoder says so instead of inventing a correction.
    const StabilizerCode code = shipped("surface_rot_3");
    ExtractionOptions x;
    x.rounds = 2;
    const MemoryExperiment ex = planMemoryExperiment(code, x).value();
    NoiseParams n;
    n.setting = NoiseSetting::CodeCapacity;
    n.dataError = DataErrorKind::BitFlip;
    n.p = 0.05;
    UnionFindDecoder uf(buildDecodingGraph(ex, planNoise(ex, n).value()).value());
    SyndromeLattice lattice{std::vector<std::uint8_t>(ex.detectors.size(), 0)};
    lattice.events[ex.detectorAt(1, 1)] = 1; // generator 1 is an X check
    const auto rejected = uf.decode(lattice);
    REQUIRE_FALSE(rejected.has_value());
    REQUIRE(rejected.error().code == err::DecodeFailed);
    // The decoder stays usable afterwards.
    lattice.events.assign(ex.detectors.size(), 0);
    lattice.events[ex.detectorAt(2, 0)] = 1; // Z check 2 next to the top boundary
    const auto ok = uf.decode(lattice);
    REQUIRE(ok.has_value());
    REQUIRE(ok->edges.size() == 1);
    REQUIRE(ok->edges[0].b == kBoundary);
    REQUIRE(ok->observableMask == 1); // qubits 2 and 5 lie on Z̄
}
