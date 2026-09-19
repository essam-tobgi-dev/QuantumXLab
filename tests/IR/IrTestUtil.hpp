#pragma once
// Shared helpers for the IR tests: parse + build from source, node access, unitary comparison.
#include "IR/IR.hpp"
#include "Lang/Sema.hpp"
#include "Numerics/Checks.hpp"
#include "Numerics/Matrix.hpp"
#include "Numerics/Tensor.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace irtest {
using namespace qlab;

inline std::string program(std::string_view body) {
    return "OPENQASM 3.0;\ninclude \"stdgates.inc\";\n" + std::string(body);
}

// Parse and build; the diagnostic text is attached to the failure.
inline Result<ir::Circuit> tryBuild(std::string_view src, const ir::ParamMap& inputs = {},
                                    const ir::BuildOptions& opts = {}) {
    auto prog = lang::parseProgram(src, "test.qasm");
    if (!prog)
        return std::unexpected(prog.error());
    return ir::buildCircuit(*prog, inputs, opts);
}

inline ir::Circuit build(std::string_view src, const ir::ParamMap& inputs = {},
                         const ir::BuildOptions& opts = {}) {
    auto c = tryBuild(src, inputs, opts);
    INFO(std::string(src) << "\n" << (c ? std::string() : c.error().format()));
    REQUIRE(c.has_value());
    auto st = ir::verify(*c);
    INFO((st ? std::string() : st.error().format()));
    REQUIRE(st.has_value());
    return std::move(*c);
}

// Top-level nodes in topological order.
inline std::vector<const ir::Node*> nodes(const ir::Circuit& c) {
    std::vector<const ir::Node*> v;
    for (auto id : c.topologicalOrder())
        v.push_back(&c.node(id));
    return v;
}

inline const ir::Gate& gateAt(const ir::Circuit& c, std::size_t i) {
    auto v = nodes(c);
    REQUIRE(i < v.size());
    REQUIRE(std::holds_alternative<ir::Gate>(*v[i]));
    return std::get<ir::Gate>(*v[i]);
}

inline std::vector<std::uint32_t> indices(const std::vector<ir::Wire>& ws) {
    std::vector<std::uint32_t> v;
    for (auto w : ws)
        v.push_back(w.index);
    return v;
}

inline num::Matrix unitary(const ir::Circuit& c, std::uint32_t n = 0) {
    auto u = ir::toUnitary(c, n);
    INFO((u ? std::string() : u.error().format()));
    REQUIRE(u.has_value());
    return std::move(*u);
}

inline bool sameUnitary(const num::Matrix& a, const num::Matrix& b, double tol = 1e-12) {
    return num::approxEqual(a, b, tol);
}

} // namespace irtest
