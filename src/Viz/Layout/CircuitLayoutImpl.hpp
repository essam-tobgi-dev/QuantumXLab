#pragma once
// INTERNAL to the circuit layout (CircuitGlyphs.cpp ↔ CircuitLayout.cpp); not part of Viz.hpp.
#include "Viz/Layout/CircuitLayout.hpp"

namespace qlab::viz::layout::detail {

// Row of the classical register that holds flat bit `bit` (rows follow the qubit wires), or
// nullopt when no declared register contains it.
std::optional<std::uint32_t> classicalRowOf(const ir::Circuit& top, std::uint32_t bit);

// Kind, label, parameters and rows of one node — no position yet. `top` owns the registers (nested
// bodies carry none). Returns nullopt for a node that touches nothing drawable (gphase).
std::optional<Glyph> describeNode(const ir::Circuit& top, const ir::Node& node, std::uint32_t qubitRows);

// Width of a glyph in layout units, from its kind and text.
double glyphWidth(const Glyph& g);

// Nested bodies of a control node, in drawing order, with the text that separates them
// ("else" before the second body of a branch).
struct Body {
    const ir::Circuit* circuit = nullptr;
    std::string separator;
};
std::vector<Body> bodiesOf(const ir::Node& node);

} // namespace qlab::viz::layout::detail
