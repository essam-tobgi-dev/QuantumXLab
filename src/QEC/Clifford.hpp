#pragma once
// Internal helper of qlab::qec: conjugation of a Hermitian Pauli string by one Clifford gate of the
// QEC gate set (T09 §1.3 and the tableau rules of T09 §3.1, signs included).
#include "QEC/Extraction.hpp"
#include <utility>

namespace qlab::qec::detail {

inline void flipSign(PauliString& p) { p.phase = static_cast<std::int8_t>((p.phase + 2) & 3); }

inline void conjugateCx(PauliString& p, std::uint32_t c, std::uint32_t t) {
    if (p.x[c] & p.z[t] & (p.x[t] ^ p.z[c] ^ 1u)) flipSign(p);   // T09 §3.1 CNOT rule
    p.x[t] ^= p.x[c];
    p.z[c] ^= p.z[t];
}
inline void conjugateH(PauliString& p, std::uint32_t a) {
    if (p.x[a] & p.z[a]) flipSign(p);   // H Y H = −Y
    std::swap(p.x[a], p.z[a]);
}
inline void conjugateS(PauliString& p, std::uint32_t a) {
    if (p.x[a] & p.z[a]) flipSign(p);   // S Y S† = −X, S X S† = Y
    p.z[a] ^= p.x[a];
}
inline void conjugateSdg(PauliString& p, std::uint32_t a) {
    if (p.x[a] & (p.z[a] ^ 1u)) flipSign(p);   // S† X S = −Y, S† Y S = X
    p.z[a] ^= p.x[a];
}

// p ← U p U† for the gate `op`; false when the op is not unitary (reset, measure) or out of range.
inline bool conjugate(PauliString& p, const Op& op) {
    if (op.marker()) return true;
    if (op.a >= p.n || (op.twoQubit() && op.b >= p.n)) return false;
    const std::uint32_t a = op.a, b = op.b;
    switch (op.kind) {
    case OpKind::H: conjugateH(p, a); return true;
    case OpKind::S: conjugateS(p, a); return true;
    case OpKind::Sdg: conjugateSdg(p, a); return true;
    case OpKind::X: if (p.z[a]) flipSign(p); return true;
    case OpKind::Y: if (p.x[a] ^ p.z[a]) flipSign(p); return true;
    case OpKind::Z: if (p.x[a]) flipSign(p); return true;
    case OpKind::CX: conjugateCx(p, a, b); return true;
    case OpKind::CZ: conjugateH(p, b); conjugateCx(p, a, b); conjugateH(p, b); return true;
    case OpKind::CY: conjugateSdg(p, b); conjugateCx(p, a, b); conjugateS(p, b); return true;   // CY = S_t CX S†_t
    case OpKind::Swap: std::swap(p.x[a], p.x[b]); std::swap(p.z[a], p.z[b]); return true;
    default: return false;
    }
}

} // namespace qlab::qec::detail
