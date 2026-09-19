#pragma once
// Spec 13 §4 — the built-in `stdgates.inc` table and hover documentation (§11).
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::lang {

enum class GateKind { Builtin, Standard, Legacy, Native };

struct GateInfo {
    std::string_view name;
    int nParams;
    int nQubits;
    GateKind kind;
    bool isRotation; // pow(k) with real k allowed (rx, ry, rz, p, cp, crx, cry, crz, u1, phase,
                     // gphase)
    std::string_view signature;
    std::string_view description;
    std::string_view matrixId; // id in Assets/Theory/gates.json
};

struct GateDoc {
    std::string name, signature, description, matrixId;
};

class StdGates {
  public:
    static const std::vector<GateInfo>& all();
    static const GateInfo* find(std::string_view name);
    static bool isStd(std::string_view name) { return find(name) != nullptr; }
    static std::optional<GateDoc> doc(std::string_view name);
    // OpenQASM 3 source of stdgates.inc as reproduced in Assets/Programs/Include/stdgates.inc.
    static std::string_view source();
};

} // namespace qlab::lang
