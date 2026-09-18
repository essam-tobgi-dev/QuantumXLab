// Spec 14 §11 — parse → build → toQasm → parse → build reproduces the dump for every example program
// the frontend accepts, and for synthetic programs that exercise every node kind and spelling.
#include "IrTestUtil.hpp"
#include "Core/Paths.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace qlab;
using namespace irtest;
namespace fs = std::filesystem;

namespace {
std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Spec 14 §2: every node, nested ones included, keeps the span of the statement it came from.
std::size_t countSpanless(const ir::Circuit& c, const std::string& file) {
    std::size_t bad = 0;
    for (auto id : c.topologicalOrder()) {
        const ir::Node& n = c.node(id);
        const SourceSpan& sp = ir::nodeSpan(n);
        if (sp.line == 0 || sp.file != file) ++bad;
        if (const auto* b = std::get_if<ir::Branch>(&n)) bad += countSpanless(*b->thenBody, file) + countSpanless(*b->elseBody, file);
        if (const auto* l = std::get_if<ir::Loop>(&n)) bad += countSpanless(*l->body, file);
        if (const auto* x = std::get_if<ir::Box>(&n)) bad += countSpanless(*x->body, file);
    }
    return bad;
}

void roundTrip(const ir::Circuit& c, std::string_view label) {
    INFO(label);
    auto qasm = ir::toQasm(c);
    INFO((qasm ? std::string() : qasm.error().format()));
    REQUIRE(qasm.has_value());
    INFO("emitted OpenQASM:\n" << *qasm);
    auto again = tryBuild(*qasm);
    INFO((again ? std::string() : again.error().format()));
    REQUIRE(again.has_value());
    CHECK(ir::verify(*again).has_value());
    CHECK(ir::dump(*again) == ir::dump(c));
    CHECK(again->structurallyEqual(c, 0.0));   // parameters survive bit-exactly
    auto second = ir::toQasm(*again);
    REQUIRE(second.has_value());
    if (*second != *qasm) {
        // Show the first differing line rather than two walls of text.
        std::size_t i = 0;
        while (i < second->size() && i < qasm->size() && (*second)[i] == (*qasm)[i]) ++i;
        const auto lineOf = [](const std::string& t, std::size_t pos) {
            const std::size_t b = t.rfind('\n', pos) + 1;
            const std::size_t e = t.find('\n', pos);
            return t.substr(b, (e == std::string::npos ? t.size() : e) - b);
        };
        UNSCOPED_INFO("first emission: " << lineOf(*qasm, i));
        UNSCOPED_INFO("second emission: " << lineOf(*second, i));
    }
    CHECK(*second == *qasm);                    // emission is a fixpoint
}
} // namespace

TEST_CASE("every example program the frontend accepts round-trips with an identical dump") {
    // Both shipped corpora: the examples library and the calibration experiments. The
    // calibration set was excluded before, which let `t2_ramsey.qasm` parse cleanly yet fail to
    // build (a duration ratio the folder did not support) without any test noticing.
    std::vector<fs::path> files;
    for (const char* sub : {"Examples", "Calibration"}) {
        const fs::path root = core::assetDir() / "Programs" / sub;
        if (!fs::is_directory(root)) continue;
        for (const auto& e : fs::recursive_directory_iterator(root))
            if (e.is_regular_file() && e.path().extension() == ".qasm") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    REQUIRE(files.size() >= 30);
    std::vector<std::string> skipped;
    std::size_t built = 0;
    for (const auto& f : files) {
        const lang::Program prog = lang::analyzeProgram(slurp(f), f.filename().string());
        if (!prog.ok()) {
            skipped.push_back(f.filename().string());
            continue;
        }
        auto c = ir::buildCircuit(prog);
        INFO(f.filename().string() << (c ? std::string() : "\n" + c.error().format()));
        REQUIRE(c.has_value());
        CHECK(ir::verify(*c).has_value());
        CHECK(countSpanless(*c, f.filename().string()) == 0);
        roundTrip(*c, f.filename().string());
        ++built;
    }
    std::string names;
    for (const auto& s : skipped) names += " " + s;
    WARN("round trip: " << built << " program(s) checked; rejected by the frontend and skipped:"
                        << (skipped.empty() ? std::string(" none") : names));
    CHECK(skipped.empty()); // every shipped program must parse
    CHECK(built + skipped.size() == files.size());
    CHECK(built >= 30);
}

TEST_CASE("synthetic programs round-trip: modifiers, global phase, natives and gate spellings") {
    const auto c = build(program(R"(qubit[3] q;
qubit s;
gphase(0.25);
negctrl @ ctrl @ x q[0], q[1], q[2];
inv @ iswap q[0], q[1];
pow(3) @ siswap q[1], q[2];
inv @ sx s;
ctrl @ U(0.1, -0.2, 0.3) q[0], s;
negctrl @ gphase(-1.5) q[2];
ctrl(2) @ rzz(1e-7) q[0], q[1], q[2], s;
rx(-0.0) q[0];
cu(0.1, 0.2, 0.3, 0.4) q[1], q[0];
ecr q[0], q[1];
ms(0.5) q[1], q[2];
id q[0];
CX q[2], q[0];
u2(0.5, 1e+300) s;
ctrl @ inv @ siswap s, q[0], q[1];
)"));
    CHECK(gateAt(c, 6).name == "sxdg");   // gphase, x, iswap†, 3 × siswap, then inv @ sx
    roundTrip(c, "gates");
}

TEST_CASE("synthetic programs round-trip: measurement, classical storage, branches and loops") {
    roundTrip(build(program(R"(qubit[3] q;
bit[2] m;
bit flag;
int k = 0;
uint[4] u;
m[0] = measure q[0];
m[1] = measure q[1];
measure q[2];
if ((m == 3) || (m[0] != 1)) { x q[2]; k = k + 2; } else { z q[2]; }
flag = measure q[2];
u = m + 1;
if (!(k > 1) && (u == 2)) { reset q[0]; } else { }
while (flag == 1) { h q[2]; flag = measure q[2]; }
m[0:1] = -u;
)")), "classical");
    roundTrip(build(program(R"(def bell(qubit a, qubit b) -> bit { h a; cx a, b; return measure a; }
qubit[2] q;
bit c;
c = bell(q[0], q[1]);
for int i in [0:1] {
  bit t = measure q[i];
  if (t == 1) { x q[i]; }
}
)")), "temporaries and block-local storage");
}

TEST_CASE("synthetic programs round-trip: durations, boxes, barriers") {
    roundTrip(build(program(R"(qubit[2] q;
delay[100ns] q[0];
delay[2.345ns - 3dt] q;
delay[-4dt];
box[1.5ms] { x q[0]; delay[20ns]; barrier q[0]; }
box { y q[1]; }
barrier;
barrier q[1], q[0];
)")), "timing");
}

TEST_CASE("synthetic programs round-trip: physical qubits, calibrations and defcal-only gates") {
    const auto c = build(R"(OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
pragma qlab.layout physical
pragma qlab.shots 100
input float amp = 0.25;
const float width = 40.0;
cal {
  extern port d0;
  frame f0 = newframe(d0, 5.1e9, 0.0);
}
defcal pulse(angle a) $0 {
  play(f0, gaussian(amp * a, 40ns, 10ns));
  shift_phase(f0, -pi / 2);
  delay[width * 1ns] f0;
}
defcal measure $1 -> bit {
  return capture_v2(f0, 2ms);
}
bit[2] c;
pulse(0.5) $0;
ctrl @ pulse(0.5) $1, $0;
x $1;
c[0] = measure $0;
c[1] = measure $1;
)");
    CHECK(c.isPhysical());
    CHECK((gateAt(c, 0).opaque && gateAt(c, 1).opaque && gateAt(c, 1).controls.size() == 1));
    REQUIRE(c.meta().contains("calibrations"));
    CHECK(c.meta()["calibrations"].size() == 3);
    const std::string defcal = c.meta()["calibrations"][1].get<std::string>();
    CHECK(defcal.find("(0.25 * a)") != std::string::npos);   // the input is bound, the parameter kept
    CHECK(defcal.find("(40.0 * 1ns)") != std::string::npos);
    roundTrip(c, "physical");
    const auto virt = build(program("defcal xb(angle b) $0 { play(f, drag(0.5, 24ns, 6ns, b)); }\nqubit q;\nxb(0.1) q;\n"));
    CHECK(gateAt(virt, 0).opaque);
    roundTrip(virt, "defcal-only gate on a virtual qubit");
}

TEST_CASE("toQasm refuses what OpenQASM 3 cannot spell") {
    ir::Circuit c;
    c.setQubitCount(1);
    auto u = ir::makeUnitary(num::Matrix::identity(2), {ir::Wire{0}});
    REQUIRE(u.has_value());
    c.add(*u);
    auto custom = ir::toQasm(c);
    REQUIRE_FALSE(custom.has_value());
    CHECK(custom.error().code == ir::err::Unsupported);
    ir::Circuit f;
    f.setQubitCount(2);
    auto g = ir::makeGate("fsim", {ir::Wire{0}, ir::Wire{1}}, {0.1, 0.2});
    REQUIRE(g.has_value());
    f.add(*g);
    CHECK_FALSE(ir::toQasm(f).has_value());
    // A register-less API circuit gets synthesized registers and still re-parses.
    ir::Circuit api;
    api.setQubitCount(2);
    api.setClbitCount(1);
    api.add(*ir::makeGate("cx", {ir::Wire{0}, ir::Wire{1}}));
    api.add(ir::Measure{ir::Wire{1}, ir::ClassicalBit{0}, std::nullopt, {}});
    auto text = ir::toQasm(api);
    REQUIRE(text.has_value());
    CHECK(text->find("qubit[2] q;") != std::string::npos);
    CHECK(tryBuild(*text).has_value());
}
