#pragma once
// Shared helpers of the Compiler tests: circuits from gate lists or source text, unitaries,
// shipped devices loaded once per test binary, seeded random angles.
#include <catch2/catch_test_macros.hpp>
#include "Compiler/Compiler.hpp"
#include "Core/Json.hpp"
#include "Core/Paths.hpp"
#include "Core/Random.hpp"
#include "Hardware/Hardware.hpp"
#include "IR/IR.hpp"
#include "Lang/Sema.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Matrix.hpp"
#include <map>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace ctest {
using namespace qlab;
inline constexpr double kPi = std::numbers::pi;

inline ir::Wire W(std::uint32_t i) { return ir::Wire{i}; }

inline ir::Gate G(std::string_view name, std::vector<std::uint32_t> wires, std::vector<double> params = {}) {
    std::vector<ir::Wire> ws;
    for (auto w : wires) ws.push_back(ir::Wire{w});
    auto g = ir::makeGate(name, std::move(ws), std::move(params));
    INFO("gate " << name << ": " << (g ? std::string() : g.error().format()));
    REQUIRE(g.has_value());
    return std::move(*g);
}
// ctrl/negctrl @ gate: `controls` precede, `negative[j]` marks a |0⟩-activated control.
inline ir::Gate controlled(ir::Gate g, std::vector<std::uint32_t> controls, std::vector<std::uint8_t> negative = {}) {
    for (auto c : controls) g.controls.push_back(ir::Wire{c});
    g.negControl = std::move(negative);
    g.matrixCache.reset();
    return g;
}

inline ir::Circuit circuit(std::uint32_t nQubits, std::vector<ir::Gate> gates, bool physical = false) {
    ir::Circuit c;
    c.setQubitCount(nQubits);
    c.setPhysical(physical);
    for (auto& g : gates) c.add(std::move(g));
    return c;
}

inline std::string program(std::string_view body) {
    return "OPENQASM 3.0;\ninclude \"stdgates.inc\";\n" + std::string(body);
}
inline lang::Program parse(std::string_view src) {
    auto p = lang::parseProgram(src, "test.qasm");
    INFO(std::string(src) << "\n" << (p ? std::string() : p.error().format()));
    REQUIRE(p.has_value());
    return std::move(*p);
}
inline ir::Circuit build(std::string_view src, const ir::ParamMap& inputs = {}) {
    const auto prog = parse(src);
    auto c = ir::buildCircuit(prog, inputs);
    INFO(std::string(src) << "\n" << (c ? std::string() : c.error().format()));
    REQUIRE(c.has_value());
    return std::move(*c);
}

inline num::Matrix unitary(const ir::Circuit& c, std::uint32_t n = 0) {
    auto u = ir::toUnitary(c, n);
    INFO((u ? std::string() : u.error().format()));
    REQUIRE(u.has_value());
    return std::move(*u);
}

inline std::vector<const ir::Gate*> gatesOf(const ir::Circuit& c) {
    std::vector<const ir::Gate*> v;
    for (auto id : c.topologicalOrder())
        if (const auto* g = std::get_if<ir::Gate>(&c.node(id))) v.push_back(g);
    return v;
}
inline std::size_t countGates(const ir::Circuit& c, std::string_view name) {
    std::size_t n = 0;
    for (const auto* g : gatesOf(c))
        if (g->name == name) ++n;
    return n;
}

inline const hw::LoadedDevice& device(const std::string& id) {
    static std::map<std::string, std::unique_ptr<hw::LoadedDevice>> cache;
    auto it = cache.find(id);
    if (it == cache.end()) {
        auto d = hw::loadShippedDevice(id);
        if (!d) FAIL("loading " << id << ": " << d.error().format());
        it = cache.emplace(id, std::make_unique<hw::LoadedDevice>(std::move(*d))).first;
    }
    return *it->second;
}

inline compiler::Target targetOf(const std::string& deviceId, std::string_view entangler = {}) {
    auto t = compiler::Target::forDevice(device(deviceId).device, entangler);
    INFO((t ? std::string() : t.error().format()));
    REQUIRE(t.has_value());
    return std::move(*t);
}

// Uniform angle in (−2π, 2π): wider than one period so that wrapping is exercised.
inline double angle(core::Random& rng) { return rng.uniform(-2.0 * kPi, 2.0 * kPi); }

} // namespace ctest
