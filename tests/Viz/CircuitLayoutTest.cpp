// Spec 21 §3.13, §5 — circuit diagram layout: moments = ir::Circuit::layers(), overlap-free
// sub-columns, glyph kinds, angle text, measurement reach, regions, timed layout. Headless.
#include "Viz/Layout/CircuitLayout.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <numbers>

using namespace qlab;
using namespace qlab::viz::layout;
using Catch::Approx;

namespace {
constexpr double kPi = std::numbers::pi;

void gate(ir::Circuit& c, const char* name, std::vector<std::uint32_t> wires,
          std::vector<double> params = {}) {
    std::vector<ir::Wire> ws;
    for (auto w : wires)
        ws.emplace_back(w);
    auto g = ir::makeGate(name, std::move(ws), std::move(params));
    REQUIRE(g.has_value());
    c.add(*g);
}

// Textbook QFT on 4 qubits, most significant qubit first, with the final swaps.
ir::Circuit qft4() {
    ir::Circuit c;
    c.setQubitCount(4);
    c.addQubitRegister({"q", 0, 4, false});
    for (int t = 3; t >= 0; --t) {
        gate(c, "h", {static_cast<std::uint32_t>(t)});
        for (int k = t - 1; k >= 0; --k)
            gate(c, "cp", {static_cast<std::uint32_t>(k), static_cast<std::uint32_t>(t)},
                 {kPi / static_cast<double>(1 << (t - k))});
    }
    gate(c, "swap", {0, 3});
    gate(c, "swap", {1, 2});
    return c;
}

// Glyphs are stored moment by moment, not in program order: look one up by its position in the
// circuit's topological order.
const Glyph& byTopo(const CircuitLayout& lay, std::uint32_t topoIndex) {
    for (const Glyph& g : lay.glyphs)
        if (g.depth == 0 && g.topoIndex == topoIndex)
            return g;
    FAIL("no glyph with topological index " << topoIndex);
    return lay.glyphs.front();
}
} // namespace

TEST_CASE("QFT-4 lays out in 8 moments and 11 columns with no overlapping boxes") {
    const ir::Circuit c = qft4();
    REQUIRE(c.nodeCount() == 12);
    // ASAP by hand: h3 | cp23 | cp13,h2 | cp03,cp12 | cp02,h1 | cp01 | h0,swap12 | swap03.
    REQUIRE(c.layers().size() == 8);
    auto lay = layoutCircuit(c);
    REQUIRE(lay.has_value());
    CHECK(lay->moments == 8); // the moment layout IS ir::Circuit::layers() (spec 21 §5)
    // Three moments hold a gate that crosses another gate's wire — cp(1,3) over h on q2, cp(0,3)
    // over cp(1,2), cp(0,2) over h on q1 — and each splits into two columns: 8 + 3 = 11. (h0 and
    // swap(1,2) share moment 6 on disjoint wires and stay in one column.)
    CHECK(lay->columns == 11);
    std::vector<std::vector<std::uint32_t>> columnsOfMoment(8);
    for (const Glyph& g : lay->glyphs) {
        auto& cols = columnsOfMoment[g.moment];
        if (std::find(cols.begin(), cols.end(), g.column) == cols.end())
            cols.push_back(g.column);
    }
    for (std::size_t m = 0; m < 8; ++m)
        CHECK(columnsOfMoment[m].size() == ((m == 2 || m == 3 || m == 4) ? 2u : 1u));
    CHECK_FALSE(lay->firstOverlap().has_value());
    CHECK(lay->glyphs.size() == 12);
    CHECK(lay->rows.size() == 4);
    CHECK(lay->rows[2].name == "q[2]");
    // Every glyph sits in the moment layers() assigns to its node, and columns never run backwards.
    const auto layers = c.layers();
    for (const Glyph& g : lay->glyphs) {
        REQUIRE(g.moment < layers.size());
        CHECK(std::find(layers[g.moment].begin(), layers[g.moment].end(), g.node) !=
              layers[g.moment].end());
    }
    for (const Glyph& a : lay->glyphs)
        for (const Glyph& b : lay->glyphs)
            if (a.moment < b.moment)
                CHECK(a.column < b.column);
    // Glyph content: cp is a controlled box labelled with its base gate and angle as a multiple of
    // π.
    const Glyph& cp = byTopo(*lay, 1); // cp(π/2) q[2], q[3]
    CHECK(cp.kind == GlyphKind::Box);
    CHECK(cp.label == "p");
    CHECK(cp.params == "π/2");
    CHECK(cp.controlRows == std::vector<std::uint32_t>{2});
    CHECK(cp.targetRows == std::vector<std::uint32_t>{3});
    CHECK(byTopo(*lay, 10).kind == GlyphKind::Swap);
    CHECK(byTopo(*lay, 11).kind == GlyphKind::Swap);
    CHECK(lay->glyphAt(cp.bounds.cx(), cp.bounds.cy()) == &cp);
    CHECK(lay->glyphAt(-5.0, -5.0) == nullptr);
    CHECK(lay->width > 0.0);
    CHECK(lay->height == Approx(4.0));
}

TEST_CASE("glyph kinds: cx, cz, ccx, measurement reach, reset, barrier, angles") {
    ir::Circuit c;
    c.setQubitCount(3);
    c.addQubitRegister({"q", 0, 3, false});
    c.setClbitCount(3);
    c.addBitRegister({"c", 0, 3, ir::RegKind::Bit, false});
    gate(c, "cx", {0, 2});
    gate(c, "cz", {0, 1});
    gate(c, "ccx", {0, 1, 2});
    gate(c, "rz", {1}, {0.1234567});
    gate(c, "ry", {1}, {-3 * kPi / 8});
    c.add(ir::Reset{ir::Wire{2}, std::nullopt, {}});
    c.add(ir::Barrier{{}, {}});
    c.add(ir::Measure{ir::Wire{0}, ir::ClassicalBit{0}, std::nullopt, {}});
    c.add(ir::Measure{ir::Wire{1}, ir::ClassicalBit{1}, std::nullopt, {}});
    auto lay = layoutCircuit(c);
    REQUIRE(lay.has_value());
    CHECK_FALSE(lay->firstOverlap().has_value());
    REQUIRE(lay->glyphs.size() == 9);
    const auto find = [&](GlyphKind k, std::size_t nth = 0) -> const Glyph& {
        for (const Glyph& g : lay->glyphs)
            if (g.kind == k && nth-- == 0)
                return g;
        FAIL("glyph kind not found");
        return lay->glyphs.front();
    };
    const Glyph& cx = find(GlyphKind::Cx);
    CHECK(cx.controlRows == std::vector<std::uint32_t>{0});
    CHECK(cx.targetRows == std::vector<std::uint32_t>{2});
    CHECK(cx.rowMin == 0);
    CHECK(cx.rowMax == 2); // it crosses q[1]: that wire is occupied
    CHECK(find(GlyphKind::Cz).controlRows.size() == 1);
    CHECK(find(GlyphKind::Cx, 1).controlRows.size() == 2); // ccx: two dots and a target
    CHECK(find(GlyphKind::Box).params == "0.1235");        // not a multiple of π: radians, 4 s.f.
    CHECK(find(GlyphKind::Box, 1).params == "-3π/8");
    CHECK(find(GlyphKind::Reset).label == "|0>");
    const Glyph& barrier = find(GlyphKind::Barrier);
    CHECK(barrier.rowMin == 0);
    CHECK(barrier.rowMax == 2); // empty wire list = every wire
    // A measurement's double line runs down to the classical register row (row 3): the two
    // measurements cannot share a column even though they are in the same moment.
    const Glyph &m0 = find(GlyphKind::Measure), &m1 = find(GlyphKind::Measure, 1);
    CHECK(m0.classicalRow == std::optional<std::uint32_t>{3});
    CHECK(m0.rowMax == 3);
    CHECK(m0.params == "c[0]");
    CHECK(m0.moment == m1.moment);
    CHECK(m0.column != m1.column);
    REQUIRE(lay->rows.size() == 4);
    CHECK(lay->rows[3].classical);
    CHECK(lay->rows[3].bits == 3);
}

TEST_CASE("physical view: wire labels carry the layout, routing SWAPs are marked") {
    ir::Circuit c;
    c.setQubitCount(3);
    c.setPhysical(true);
    gate(c, "h", {1});
    gate(c, "swap", {1, 2});
    gate(c, "cx", {2, 0});
    const std::vector<std::uint32_t> virtualToPhysical{1, 0}; // program q0 starts on physical 1
    CircuitLayoutOptions o;
    o.layout = virtualToPhysical;
    o.markRoutingSwaps = true;
    auto lay = layoutCircuit(c, o);
    REQUIRE(lay.has_value());
    CHECK(lay->rows[1].physical == std::optional<std::uint32_t>{1});
    CHECK(lay->rows[1].virtualQubit == std::optional<std::uint32_t>{0});
    CHECK(lay->rows[0].virtualQubit == std::optional<std::uint32_t>{1});
    CHECK_FALSE(lay->rows[2].virtualQubit.has_value());
    CHECK(lay->routingSwaps == 1);
    CHECK(byTopo(*lay, 1).routingSwap);
    CHECK_FALSE(byTopo(*lay, 0).routingSwap);
    CHECK(countSwaps(c) == 1);
}

TEST_CASE("classical control: a branch is a bracketed region around its body") {
    ir::Circuit body;
    body.setQubitCount(2);
    body.setClbitCount(1);
    gate(body, "x", {1});
    gate(body, "z", {1});
    ir::Circuit c;
    c.setQubitCount(2);
    c.addQubitRegister({"q", 0, 2, false});
    c.setClbitCount(1);
    c.addBitRegister({"c", 0, 1, ir::RegKind::Bit, true});
    gate(c, "h", {0});
    c.add(ir::Measure{ir::Wire{0}, ir::ClassicalBit{0}, std::nullopt, {}});
    ir::Branch br{ir::ClassicalExpr::binary(ir::ClassOp::Eq,
                                            ir::ClassicalExpr::bitRef(ir::ClassicalBit{0}),
                                            ir::ClassicalExpr::constant(1)),
                  ir::SubCircuit(body),
                  ir::SubCircuit(),
                  {}};
    c.add(std::move(br));
    auto lay = layoutCircuit(c);
    REQUIRE(lay.has_value());
    CHECK_FALSE(lay->firstOverlap().has_value());
    const Glyph* region = nullptr;
    std::vector<const Glyph*> inside;
    for (const Glyph& g : lay->glyphs) {
        if (g.kind == GlyphKind::Region)
            region = &g;
        if (g.depth == 1)
            inside.push_back(&g);
    }
    REQUIRE(region != nullptr);
    CHECK(region->label.rfind("if (", 0) == 0);
    CHECK(region->columnSpan == 2);
    REQUIRE(inside.size() == 2);
    for (const Glyph* g :
         inside) { // the bracket encloses its body; body glyphs share the branch's playhead index
        CHECK(region->bounds.contains(g->bounds.cx(), g->bounds.cy()));
        CHECK(g->topoIndex == region->topoIndex);
    }
    CHECK(inside[0]->column < inside[1]->column);
    CHECK(lay->glyphAt(inside[0]->bounds.cx(), inside[0]->bounds.cy()) ==
          inside[0]); // the gate wins over its region
}

TEST_CASE("timed layout places boxes at t_start with widths proportional to duration") {
    ir::Circuit c;
    c.setQubitCount(3);
    c.setPhysical(true);
    gate(c, "sx", {0});
    gate(c, "rz", {0}, {kPi / 2});
    gate(c, "sx", {1});
    gate(c, "cx", {0, 2});
    CHECK_FALSE(layoutCircuit(c, {.timed = true}).has_value()); // not scheduled yet
    // The record compiler::schedule writes, parallel to topologicalOrder(): sx 40 ns, rz virtual,
    // sx 40 ns on q1, cx 300 ns on (0, 2) running while q1 idles after its sx.
    c.meta()["schedule"] = {{"start_ps", {0, 40000, 0, 40000}},
                            {"length_ps", {40000, 0, 40000, 300000}}};
    auto times = scheduleTimes(c);
    REQUIRE(times.size() == 4);
    CHECK(times[3].startNs == Approx(40.0));
    CHECK(times[3].durationNs == Approx(300.0));
    auto lay = layoutCircuit(c, {.timed = true});
    REQUIRE(lay.has_value());
    CHECK(lay->timed);
    CHECK(lay->nsPerUnit == Approx(40.0)); // the shortest real gate is one unit wide
    CHECK(lay->durationNs == Approx(340.0));
    CHECK_FALSE(lay->firstOverlap().has_value());
    const Glyph &sx = byTopo(*lay, 0), &rz = byTopo(*lay, 1), &cx = byTopo(*lay, 3);
    CHECK(sx.bounds.width() == Approx(1.0));
    CHECK(cx.bounds.x0 - sx.bounds.x0 == Approx(1.0)); // starts 40 ns later
    CHECK(cx.parts.size() == 2); // one box per wire: q1 stays free between them
    CHECK(cx.parts[0].width() == Approx(7.5));
    CHECK(rz.parts[0].y1 < sx.parts[0].y0 + 1e-9); // the virtual gate is a tick above the box band
    CHECK(*rz.durationNs == Approx(0.0));
}
