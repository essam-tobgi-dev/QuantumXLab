#pragma once
// Spec 14 §4.1 — the native gate set a compile lowers to, resolved from `device.json`.
#include "Compiler/Euler.hpp"
#include "Core/Error.hpp"
#include "Hardware/Device.hpp"
#include "IR/Node.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace qlab::compiler {

struct Target {
    Basis1q basis = Basis1q::U;
    // Two-qubit gate Decompose emits, and the rule family it belongs to (Rules.hpp):
    // cx | cz | ecr | siswap | rxx. `ms` is the device's name of `rxx` (T06 §6).
    std::string entangler = "cx";
    std::string family = "cx";
    std::vector<std::string> native1q{"U"};
    std::vector<std::string> native2q{"cx"};
    const hw::Device* device = nullptr;
    std::string name = "{U, cx}"; // device id, for QL4070
    // Largest |θ| of one `ms(θ)`: the MS pulse table of spec 10 §6.6 is calibrated up to π/2.
    double maxEntanglerAngle = 0.0;

    // Spec 14 §4.1: without a device the target is {U, cx}.
    static Target universal();
    // Native set of a device. `preferredEntangler` picks among several native two-qubit gates
    // (cx | ecr on cross-resonance devices, cz | siswap on tunable couplers) and makes it the only
    // two-qubit gate of the output; empty = emit the first listed, accept all of them.
    static Result<Target> forDevice(const hw::Device& dev,
                                    std::string_view preferredEntangler = {});

    bool isNative1q(std::string_view gate) const;
    bool isNative2q(std::string_view gate) const;
    // A node Decompose leaves alone: a plain library gate of the native set (no controls, no
    // `adjoint`, no explicit matrix) whose angle the device can play, or a defcal-only gate.
    // Directed couplers are checked separately, on physical circuits.
    bool accepts(const ir::Gate& g) const;
    // False for a native cx/ecr against the direction of a directed coupler (T02 §4 reversal).
    bool directionOk(const ir::Gate& g) const;
};

} // namespace qlab::compiler
