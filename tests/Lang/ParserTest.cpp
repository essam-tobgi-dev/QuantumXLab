#include "Lang/Lang.hpp"
#include <catch2/catch_test_macros.hpp>
using namespace qlab::lang;

static std::string diagText(const Program& p) {
    std::string s;
    for (auto& d : p.diagnostics)
        s += d.error.format() + "\n";
    return s;
}
static bool has(const Program& p, const char* id) {
    for (auto& d : p.diagnostics)
        if (d.id() == id)
            return true;
    return false;
}

static const char* kBell = R"(OPENQASM 3.0;
include "stdgates.inc";
qubit[2] q;
bit[2] c;
h q[0];
cx q[0], q[1];
c = measure q;
)";

TEST_CASE("lexer classifies tokens") {
    auto toks =
        tokenize("OPENQASM 3.0; qubit[2] q; rx(pi/2) q[0]; delay[100ns] q; $3 // c\n", "t.qasm")
            .value();
    REQUIRE(toks[0].kind == TokenKind::Keyword);
    REQUIRE(toks[1].kind == TokenKind::Number);
    REQUIRE(toks[1].isFloat);
    REQUIRE(toks[3].kind == TokenKind::Type);
    bool sawDur = false, sawPhys = false, sawBuiltin = false;
    for (auto& t : toks) {
        if (t.kind == TokenKind::Duration) {
            sawDur = true;
            REQUIRE(t.unit == DurationUnit::Ns);
            REQUIRE(t.fval == 100.0);
        }
        if (t.kind == TokenKind::PhysicalQubit) {
            sawPhys = true;
            REQUIRE(t.ival == 3);
        }
        if (t.kind == TokenKind::Builtin && t.text == "pi")
            sawBuiltin = true;
    }
    REQUIRE(sawDur);
    REQUIRE(sawPhys);
    REQUIRE(sawBuiltin);
    auto lenient = tokenizeLenient("x q; // comment\n");
    bool comment = false;
    for (auto& t : lenient)
        if (t.kind == TokenKind::Comment)
            comment = true;
    REQUIRE(comment);
    REQUIRE_FALSE(tokenize("qubit q; @@ #").has_value());
}

TEST_CASE("bell program parses and analyzes without diagnostics") {
    Program p = analyzeProgram(kBell, "bell.qasm");
    INFO(dumpAst(p.ast));
    for (auto& d : p.diagnostics)
        INFO(d.error.format());
    REQUIRE(p.diagnostics.empty());
    REQUIRE(p.qubitCount == 2);
    REQUIRE(p.bitCount == 2);
    REQUIRE(p.hasQuantumStatements);
    REQUIRE_FALSE(p.hasMidCircuitMeasurement);
    REQUIRE(p.usedGates.contains("cx"));
    std::string d = dumpAst(p.ast);
    REQUIRE(d.find("(cx q[0] q[1])") == std::string::npos); // operands dumped as index exprs
    REQUIRE(d.find("(cx (index q 0) (index q 1))") != std::string::npos);
    REQUIRE(parseProgram(kBell).has_value());
}

TEST_CASE("expressions respect precedence and fold") {
    Program p =
        analyzeProgram("OPENQASM 3.0;\nconst float a = 1 + 2 * 3 ** 2;\nconst int b = (7 % 4) << "
                       "2;\nconst bool c = a > 10 && b == 12;\nqubit q;\nU(a, b, c) q;\n");
    REQUIRE(p.ok());
    REQUIRE(p.symbol("a")->value->asDouble() == 19.0);
    REQUIRE(p.symbol("b")->value->i == 12);
    REQUIRE(p.symbol("c")->value->i == 1);
}

TEST_CASE("sema diagnostics") {
    REQUIRE(has(analyzeProgram("OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit q;\ncx q;\n"),
                "QL3150"));
    REQUIRE(has(analyzeProgram("OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit q;\nrx q;\n"),
                "QL3151"));
    REQUIRE(has(analyzeProgram("OPENQASM 3.0;\nqubit q;\nfoo q;\n"), "QL3010"));
    REQUIRE(has(analyzeProgram("OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit[2] q;\nx q[2];\n"),
                "QL3022"));
    REQUIRE(has(analyzeProgram("OPENQASM 3.0;\nextern f(int) -> int;\nqubit q;\n"), "QL3100"));
    REQUIRE(
        has(analyzeProgram("OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit q;\nx q;\nqubit r;\n"),
            "QL3001"));
    REQUIRE(has(analyzeProgram("qubit q;\n"), "QL2001"));
    REQUIRE(has(analyzeProgram("OPENQASM 3.0;\ninclude \"other.inc\";\nqubit q;\n"), "QL2002"));
    REQUIRE(has(
        analyzeProgram("OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit[2] q;\ncx q[0], q[0];\n"),
        "QL3140"));
    REQUIRE(has(analyzeProgram("OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit q;\nmeasure q;\n"),
                "QL3030"));
    REQUIRE(has(analyzeProgram(
                    "OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit q; bit[2] c;\nif (c) x q;\n"),
                "QL3080"));
    REQUIRE(has(
        analyzeProgram(
            "OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit q;\nswitch (1) { case 1 { x q; } }\n"),
        "QL3095"));
    REQUIRE(
        has(analyzeProgram(
                "OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit q;\ngate x a { U(0,0,0) a; }\n"),
            "QL3061"));
    REQUIRE(
        has(analyzeProgram(
                "OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit q;\ngate g a { g a; }\ng q;\n"),
            "QL3060"));
    REQUIRE(has(analyzeProgram("OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit q;\nfloat x = "
                               "1;\nwhile (x < 3) { h q; }\n"),
                "QL3090"));
    REQUIRE(has(analyzeProgram("OPENQASM 3.0;\nqubit q;\n$0;\n"), "QL3021"));
    REQUIRE(
        has(analyzeProgram(
                "OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit q;\npragma qlab.foo 1\nx q;\n"),
            "QL2050"));
    REQUIRE(has(analyzeProgram("OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit q;\ninput float "
                               "theta;\nrx(theta) q;\n"),
                "QL3178"));
    REQUIRE(has(
        analyzeProgram(
            "OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit q;\nconst int n = 2;\nn = 3;\nx q;\n"),
        "QL3172"));
    REQUIRE(has(analyzeProgram("OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit q; int k = 1;\nif "
                               "(true) { int k = 2; }\nx q;\n"),
                "QL3012"));
    REQUIRE(has(analyzeProgram("OPENQASM 3.0;\n"), "QL3002"));
}

TEST_CASE("error recovery reports multiple independent errors") {
    Program p = analyzeProgram("OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit[2] q;\nh q[0]\ncx "
                               "q[0], q[1];\nx ;\nmeasure q[9];\n");
    int errs = 0;
    for (auto& d : p.diagnostics)
        if (d.isError())
            ++errs;
    INFO(dumpAst(p.ast));
    for (auto& d : p.diagnostics)
        INFO(d.error.format());
    REQUIRE(errs >= 2);
}

TEST_CASE("pragmas are typed") {
    Program p = analyzeProgram(
        "OPENQASM 3.0;\ninclude \"stdgates.inc\";\npragma qlab.shots 2048\npragma qlab.device "
        "sc_heavyhex_27\npragma qlab.backend densitymatrix\npragma qlab.seed 7\ninput float "
        "theta;\npragma qlab.sweep theta from 0 to 1 step 0.25\npragma qlab.probe bloch "
        "q[0]\npragma qlab.layout physical\nx $0;\nrx(theta) $1;\n");
    INFO(diagText(p));
    REQUIRE(p.ok());
    REQUIRE(p.pragmas.shots == 2048);
    REQUIRE(p.pragmas.device == "sc_heavyhex_27");
    REQUIRE(p.pragmas.backend == BackendChoice::DensityMatrix);
    REQUIRE(p.pragmas.seed == 7);
    REQUIRE(p.pragmas.sweeps.size() == 1);
    REQUIRE(p.pragmas.sweeps[0].count() == 5);
    REQUIRE(p.pragmas.probes.size() == 1);
    REQUIRE(p.pragmas.probes[0].kind == "bloch");
    REQUIRE(p.usesPhysicalQubits);
    REQUIRE(p.inputs.size() == 1);
    REQUIRE(p.inputs[0].hasSweep);
}

TEST_CASE("modifiers, gate defs, subroutines, control flow, feedforward") {
    const char* src = R"(OPENQASM 3.0;
include "stdgates.inc";
gate mygate(a) x, y { rz(a) x; cx x, y; }
def parity(qubit[2] r) -> bit { bit b; b = measure r[0]; return b; }
qubit[3] q; bit[3] c; bit m;
mygate(pi/4) q[0], q[1];
ctrl @ x q[0], q[2];
inv @ pow(2) @ s q[1];
negctrl(2) @ z q[0], q[1], q[2];
for int i in [0:2] { h q[i]; }
for uint j in {0, 2} { x q[j]; }
int k = 0;
while (k < 3) { k = k + 1; }
m = parity(q[0:1]);
if (m == 1) { x q[2]; } else { z q[2]; }
barrier q;
c[0] = measure q[0];
measure q[1] -> c[1];
delay[50ns] q[2];
)";
    Program p = analyzeProgram(src);
    INFO(diagText(p));
    REQUIRE(p.ok());
    REQUIRE(p.userGates.contains("mygate"));
    REQUIRE(p.defs.contains("parity"));
    REQUIRE(p.hasFeedforward);
    REQUIRE(p.hasMidCircuitMeasurement);
    std::string d = dumpAst(p.ast);
    REQUIRE(d.find("negctrl(2)@ z") != std::string::npos);
    REQUIRE(d.find("inv@ pow(2)@ s") != std::string::npos);
}

TEST_CASE("OpenPulse cal and defcal parse") {
    const char* src = R"(OPENQASM 3.0;
include "stdgates.inc";
defcalgrammar "openpulse";
cal {
  extern port d0;
  frame df0 = newframe(d0, 4.8e9, 0.0);
  waveform wx = drag(0.42, 32ns, 8ns, 0.2);
}
defcal x $0 {
  play(df0, wx);
}
defcal rx(angle theta) $0 {
  shift_phase(df0, theta);
  play(df0, gaussian(0.5, 32ns, 8ns));
}
defcal measure $0 -> bit {
  play(df0, constant(0.1, 1us));
  return capture_v2(df0, 1us);
}
pragma qlab.layout physical
x $0;
)";
    Program p = analyzeProgram(src);
    INFO(diagText(p));
    REQUIRE(p.ok());
    REQUIRE(p.defcals.size() == 3);
    std::string d = dumpAst(p.ast);
    REQUIRE(d.find("(waveform wx (drag 0.42 32ns 8ns 0.2))") != std::string::npos);
    REQUIRE(d.find("(capture df0 1us -> return)") != std::string::npos);
    Program bad = analyzeProgram("OPENQASM 3.0;\ncal { extern port d0; frame f = newframe(d0, 1e9, "
                                 "0); play(f, constant(1.5, 10ns)); }\nqubit q;\n");
    REQUIRE(has(bad, "QL3120"));
}
