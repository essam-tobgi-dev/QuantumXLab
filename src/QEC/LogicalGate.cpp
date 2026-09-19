// Spec 16 §3, T09 §4.4, §8.1 — transversal logical gates. A candidate (the same single-qubit gate
// on every data qubit, or qubit-wise cx between two blocks) is accepted only if conjugation by it
// maps every generator into the stabilizer group with sign + and acts on X̄, Z̄ as the named logical
// gate.
#include "QEC/Clifford.hpp"
#include "QEC/Encoder.hpp"
#include <format>

namespace qlab::qec {
namespace {

// P = Q as operators on the code space: P·Q is a stabilizer (sign +).
bool sameLogical(std::span<const PauliString> group, const PauliString& p, const PauliString& q) {
    int sign = 0;
    return inGroup(group, p * q, &sign) && sign == 1;
}

PauliString image(const PauliString& p, const std::vector<Op>& gates) {
    PauliString out = p;
    for (const Op& op : gates)
        detail::conjugate(out, op);
    return out;
}

bool preservesGroup(std::span<const PauliString> group, const std::vector<Op>& gates) {
    for (const PauliString& g : group) {
        int sign = 0;
        if (!inGroup(group, image(g, gates), &sign) || sign != 1)
            return false;
    }
    return true;
}

// Ȳ = i X̄ Z̄ (Hermitian because X̄ and Z̄ anticommute).
PauliString logicalY(const PauliString& x, const PauliString& z) {
    PauliString y = x * z;
    y.phase = static_cast<std::int8_t>((y.phase + 1) & 3);
    return y;
}

PauliString onBlock(const PauliString& p, std::uint32_t n, bool second) {
    PauliString out = PauliString::identity(2 * n);
    out.phase = p.phase;
    for (std::uint32_t q = 0; q < n; ++q)
        out.setLetter(q + (second ? n : 0u), p.letter(q));
    return out;
}

std::vector<Op> onEveryQubit(OpKind kind, std::uint32_t n) {
    std::vector<Op> ops;
    for (std::uint32_t q = 0; q < n; ++q)
        ops.push_back({kind, q});
    return ops;
}

std::unexpected<Error> noGate(const StabilizerCode& code, LogicalGate gate, std::string_view why) {
    return fail(err::NoGate, std::format("code '{}' has no transversal logical {}: {}", code.id,
                                         logicalGateName(gate), why));
}

Result<Schedule> pauliGate(const StabilizerCode& code, const PauliString& logical) {
    Schedule s;
    s.qubits = code.n;
    for (std::uint32_t q : logical.support()) {
        const char letter = logical.letter(q);
        s.ops.push_back({letter == 'X' ? OpKind::X : (letter == 'Y' ? OpKind::Y : OpKind::Z), q});
    }
    return s;
}

Result<Schedule> hadamard(const StabilizerCode& code) {
    Schedule s{code.n, 0, onEveryQubit(OpKind::H, code.n)};
    if (!preservesGroup(code.stabilizers, s.ops))
        return noGate(code, LogicalGate::H, "h on every qubit leaves the stabilizer group");
    for (std::uint32_t l = 0; l < code.k; ++l)
        if (!sameLogical(code.stabilizers, image(code.logicalX[l], s.ops), code.logicalZ[l]) ||
            !sameLogical(code.stabilizers, image(code.logicalZ[l], s.ops), code.logicalX[l]))
            return noGate(code, LogicalGate::H,
                          "h on every qubit does not exchange the logical X and Z");
    return s;
}

Result<Schedule> phase(const StabilizerCode& code) {
    for (OpKind kind :
         {OpKind::S, OpKind::Sdg}) { // Steane: s on every qubit is S̄†, so S̄ = sdg⊗7 (T09 §4.4)
        Schedule s{code.n, 0, onEveryQubit(kind, code.n)};
        if (!preservesGroup(code.stabilizers, s.ops))
            continue;
        bool ok = true;
        for (std::uint32_t l = 0; l < code.k && ok; ++l)
            ok = sameLogical(code.stabilizers, image(code.logicalZ[l], s.ops), code.logicalZ[l]) &&
                 sameLogical(code.stabilizers, image(code.logicalX[l], s.ops),
                             logicalY(code.logicalX[l], code.logicalZ[l]));
        if (ok)
            return s;
    }
    return noGate(code, LogicalGate::S,
                  "neither s nor sdg on every qubit maps X̄ to Ȳ within the code");
}

Result<Schedule> cnot(const StabilizerCode& code) {
    const std::uint32_t n = code.n;
    std::vector<PauliString> group;
    for (bool second : {false, true})
        for (const PauliString& g : code.stabilizers)
            group.push_back(onBlock(g, n, second));
    for (bool forward :
         {true, false}) { // physical direction that acts as logical CNOT control → target
        Schedule s;
        s.qubits = 2 * n;
        for (std::uint32_t q = 0; q < n; ++q)
            s.ops.push_back(forward ? Op{OpKind::CX, q, n + q} : Op{OpKind::CX, n + q, q});
        if (!preservesGroup(group, s.ops))
            continue;
        bool ok = true;
        for (std::uint32_t l = 0; l < code.k && ok; ++l) {
            const PauliString xc = onBlock(code.logicalX[l], n, false),
                              xt = onBlock(code.logicalX[l], n, true);
            const PauliString zc = onBlock(code.logicalZ[l], n, false),
                              zt = onBlock(code.logicalZ[l], n, true);
            ok = sameLogical(group, image(xc, s.ops), xc * xt) &&
                 sameLogical(group, image(zc, s.ops), zc) &&
                 sameLogical(group, image(xt, s.ops), xt) &&
                 sameLogical(group, image(zt, s.ops), zc * zt);
        }
        if (ok)
            return s;
    }
    return noGate(code, LogicalGate::CNOT,
                  "qubit-wise cx between two blocks leaves the stabilizer group (not a CSS code)");
}

} // namespace

std::string_view logicalGateName(LogicalGate g) {
    switch (g) {
    case LogicalGate::X:
        return "X";
    case LogicalGate::Z:
        return "Z";
    case LogicalGate::H:
        return "H";
    case LogicalGate::S:
        return "S";
    case LogicalGate::CNOT:
        return "CNOT";
    }
    return "?";
}

Result<Schedule> planLogicalGate(const StabilizerCode& code, LogicalGate gate) {
    VerifyOptions quick;
    quick.checkLayout = false;
    quick.checkDistance = false;
    QXL_TRY(verifyCode(code, quick));
    if (code.k == 0)
        return fail(err::BadLogical, std::format("code '{}' stores no logical qubit", code.id));
    switch (gate) {
    case LogicalGate::X:
        return pauliGate(code, code.logicalX[0]);
    case LogicalGate::Z:
        return pauliGate(code, code.logicalZ[0]);
    case LogicalGate::H:
        return hadamard(code);
    case LogicalGate::S:
        return phase(code);
    case LogicalGate::CNOT:
        return cnot(code);
    }
    return fail(ErrorCode::InvalidArgument, "unknown logical gate");
}

Result<ir::Circuit> buildLogicalGate(const StabilizerCode& code, LogicalGate gate) {
    QXL_TRY_ASSIGN(const Schedule s, planLogicalGate(code, gate));
    ir::Circuit c;
    c.setQubitCount(s.qubits);
    if (gate == LogicalGate::CNOT) {
        c.addQubitRegister(ir::QubitRegister{"control", 0, code.n, false});
        c.addQubitRegister(ir::QubitRegister{"target", code.n, code.n, false});
    } else {
        c.addQubitRegister(ir::QubitRegister{"data", 0, code.n, false});
    }
    for (const Op& op : s.ops) {
        std::vector<ir::Wire> wires = {ir::Wire{op.a}};
        if (op.twoQubit())
            wires.push_back(ir::Wire{op.b});
        QXL_TRY_ASSIGN(ir::Gate g, ir::makeGate(opName(op.kind), std::move(wires)));
        c.add(std::move(g));
    }
    c.meta()["qec"] = {
        {"code", code.id}, {"role", "logical_gate"}, {"gate", std::string(logicalGateName(gate))}};
    return c;
}

} // namespace qlab::qec
