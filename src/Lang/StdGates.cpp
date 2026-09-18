#include "Lang/StdGates.hpp"

namespace qlab::lang {

const std::vector<GateInfo>& StdGates::all() {
    using K = GateKind;
    static const std::vector<GateInfo> t = {
        {"U", 3, 1, K::Builtin, false, "U(theta, phi, lambda) q", "General single-qubit unitary (OpenQASM 3 definition).", "U"},
        {"gphase", 1, 0, K::Builtin, true, "gphase(gamma)", "Global phase e^{i gamma}; becomes a phase gate on the control under ctrl @.", "gphase"},
        {"p", 1, 1, K::Standard, true, "p(lambda) q", "Phase gate diag(1, e^{i lambda}).", "p"},
        {"x", 0, 1, K::Standard, false, "x q", "Pauli X (bit flip).", "x"},
        {"y", 0, 1, K::Standard, false, "y q", "Pauli Y.", "y"},
        {"z", 0, 1, K::Standard, false, "z q", "Pauli Z (phase flip).", "z"},
        {"h", 0, 1, K::Standard, false, "h q", "Hadamard.", "h"},
        {"s", 0, 1, K::Standard, false, "s q", "Phase gate S = sqrt(Z).", "s"},
        {"sdg", 0, 1, K::Standard, false, "sdg q", "Adjoint of S.", "sdg"},
        {"t", 0, 1, K::Standard, false, "t q", "T gate = fourth root of Z.", "t"},
        {"tdg", 0, 1, K::Standard, false, "tdg q", "Adjoint of T.", "tdg"},
        {"sx", 0, 1, K::Standard, false, "sx q", "sqrt(X); the native pi/2 pulse on transmons.", "sx"},
        {"rx", 1, 1, K::Standard, true, "rx(theta) q", "Rotation about X: exp(-i theta X/2).", "rx"},
        {"ry", 1, 1, K::Standard, true, "ry(theta) q", "Rotation about Y: exp(-i theta Y/2).", "ry"},
        {"rz", 1, 1, K::Standard, true, "rz(theta) q", "Rotation about Z: exp(-i theta Z/2); virtual (frame phase) on hardware.", "rz"},
        {"cx", 0, 2, K::Standard, false, "cx c, t", "Controlled-X (CNOT).", "cx"},
        {"cy", 0, 2, K::Standard, false, "cy c, t", "Controlled-Y.", "cy"},
        {"cz", 0, 2, K::Standard, false, "cz a, b", "Controlled-Z (symmetric).", "cz"},
        {"cp", 1, 2, K::Standard, true, "cp(lambda) c, t", "Controlled phase.", "cp"},
        {"crx", 1, 2, K::Standard, true, "crx(theta) c, t", "Controlled rx.", "crx"},
        {"cry", 1, 2, K::Standard, true, "cry(theta) c, t", "Controlled ry.", "cry"},
        {"crz", 1, 2, K::Standard, true, "crz(theta) c, t", "Controlled rz.", "crz"},
        {"ch", 0, 2, K::Standard, false, "ch c, t", "Controlled-Hadamard.", "ch"},
        {"swap", 0, 2, K::Standard, false, "swap a, b", "Exchange two qubits (3 CNOTs).", "swap"},
        {"ccx", 0, 3, K::Standard, false, "ccx c1, c2, t", "Toffoli.", "ccx"},
        {"cswap", 0, 3, K::Standard, false, "cswap c, a, b", "Fredkin (controlled swap).", "cswap"},
        {"cu", 4, 2, K::Standard, false, "cu(theta, phi, lambda, gamma) c, t", "Controlled-U with phase gamma on the control.", "cu"},
        {"CX", 0, 2, K::Legacy, false, "CX c, t", "OpenQASM 2 CNOT alias.", "cx"},
        {"phase", 1, 1, K::Legacy, true, "phase(lambda) q", "OpenQASM 2 alias of p.", "p"},
        {"id", 0, 1, K::Legacy, false, "id q", "Identity (kept in the IR for padding semantics).", "id"},
        {"u1", 1, 1, K::Legacy, true, "u1(lambda) q", "OpenQASM 2 alias of p.", "p"},
        {"u2", 2, 1, K::Legacy, false, "u2(phi, lambda) q", "U(pi/2, phi, lambda).", "u2"},
        {"u3", 3, 1, K::Legacy, false, "u3(theta, phi, lambda) q", "Alias of U.", "U"},
        // native gates (spec 09 §2) — accepted by name so device-level programs parse
        {"ecr", 0, 2, K::Native, false, "ecr c, t", "Echoed cross-resonance gate (native, fixed-frequency transmons).", "ecr"},
        {"siswap", 0, 2, K::Native, false, "siswap a, b", "sqrt(iSWAP) (native, tunable couplers).", "siswap"},
        {"iswap", 0, 2, K::Native, false, "iswap a, b", "iSWAP.", "iswap"},
        {"ms", 1, 2, K::Native, true, "ms(theta) a, b", "Mølmer–Sørensen XX(theta) (native, trapped ions).", "ms"},
        {"rxx", 1, 2, K::Native, true, "rxx(theta) a, b", "exp(-i theta XX/2).", "rxx"},
        {"ryy", 1, 2, K::Native, true, "ryy(theta) a, b", "exp(-i theta YY/2).", "ryy"},
        {"rzz", 1, 2, K::Native, true, "rzz(theta) a, b", "exp(-i theta ZZ/2).", "rzz"},
    };
    return t;
}

const GateInfo* StdGates::find(std::string_view name) {
    for (const auto& g : all()) if (g.name == name) return &g;
    return nullptr;
}

std::optional<GateDoc> StdGates::doc(std::string_view name) {
    const GateInfo* g = find(name);
    if (!g) return std::nullopt;
    return GateDoc{std::string(g->name), std::string(g->signature), std::string(g->description), std::string(g->matrixId)};
}

std::string_view StdGates::source() {
    static constexpr std::string_view src = R"(// OpenQASM 3 stdgates.inc (reference definitions)
gate p(lambda) a { ctrl @ gphase(lambda) a; }
gate x a { U(pi, 0, pi) a; }
gate y a { U(pi, pi/2, pi/2) a; }
gate z a { p(pi) a; }
gate h a { U(pi/2, 0, pi) a; }
gate s a { pow(1/2) @ z a; }
gate sdg a { inv @ pow(1/2) @ z a; }
gate t a { pow(1/2) @ s a; }
gate tdg a { inv @ pow(1/2) @ s a; }
gate sx a { pow(1/2) @ x a; }
gate rx(theta) a { U(theta, -pi/2, pi/2) a; }
gate ry(theta) a { U(theta, 0, 0) a; }
gate rz(lambda) a { gphase(-lambda/2); U(0, 0, lambda) a; }
gate cx c, t { ctrl @ x c, t; }
gate cy a, b { ctrl @ y a, b; }
gate cz a, b { ctrl @ z a, b; }
gate cp(lambda) a, b { ctrl @ p(lambda) a, b; }
gate crx(theta) a, b { ctrl @ rx(theta) a, b; }
gate cry(theta) a, b { ctrl @ ry(theta) a, b; }
gate crz(theta) a, b { ctrl @ rz(theta) a, b; }
gate ch a, b { ctrl @ h a, b; }
gate swap a, b { cx a, b; cx b, a; cx a, b; }
gate ccx a, b, c { ctrl @ ctrl @ x a, b, c; }
gate cswap a, b, c { ctrl @ swap a, b, c; }
gate cu(theta, phi, lambda, gamma) c, t { p(gamma) c; ctrl @ U(theta, phi, lambda) c, t; }
gate CX c, t { ctrl @ x c, t; }
gate phase(lambda) q { U(0, 0, lambda) q; }
gate id a { U(0, 0, 0) a; }
gate u1(lambda) q { U(0, 0, lambda) q; }
gate u2(phi, lambda) q { gphase(-(phi+lambda)/2); U(pi/2, phi, lambda) q; }
gate u3(theta, phi, lambda) q { gphase(-(phi+lambda)/2); U(theta, phi, lambda) q; }
)";
    return src;
}

} // namespace qlab::lang
