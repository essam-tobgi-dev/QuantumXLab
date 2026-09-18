#pragma once
// Spec 14 §4.3 — multi-qubit decomposition rules as data: source gate, the entangler family the
// sequence is written in, the gate sequence, and the exact global phase. `decompositionRulesJson`
// serialises the table in the layout of `Assets/Theory/decompositions.json`.
#include "Core/Error.hpp"
#include "IR/Node.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::compiler {

// An angle affine in the source gate's parameters: c0 + Σ_k c[k]·p[k].
struct Affine {
    double c0 = 0.0;
    std::array<double, 4> c{};
    double eval(std::span<const double> p) const;
    std::string text() const;               // "0.5*p1 + 0.5*p2 + p3", "pi/2", "0"
    bool zero() const;
};

struct RuleStep {
    std::string_view gate;                  // library gate name
    std::vector<std::uint8_t> operands;     // indices into the source gate's operand list
    std::vector<Affine> params;
};

struct Rule {
    std::string_view source;                // library gate the rule rewrites
    // "cx": the generic rule, written with single-qubit gates, cx and other generic sources.
    // Otherwise the rule applies only when the target's entangler family matches (Target.hpp).
    std::string_view family;
    std::vector<RuleStep> steps;            // TIME order: steps[0] acts first
    Affine phase;                           // source = e^{i·phase} · steps[n−1] ⋯ steps[0], exactly
    std::string_view cite;                  // theory anchor
};

const std::vector<Rule>& decompositionRules();
// The rule for `source` on a target of the given family: the family's own rule if there is one,
// else the generic one; nullptr when the gate has no rule.
const Rule* findRule(std::string_view source, std::string_view family);
const Rule* findGenericRule(std::string_view source);

// Instantiates a rule on a gate's operands and parameters. Every produced gate carries the
// source gate's span (spec 14 §2).
struct RuleExpansion {
    std::vector<ir::Gate> gates;            // TIME order
    double phase = 0.0;
};
Result<RuleExpansion> applyRule(const Rule& rule, const ir::Gate& source);

// The table as the JSON document of spec 14 §4.3 (envelope kind "theory.decompositions").
std::string decompositionRulesJson();

} // namespace qlab::compiler
