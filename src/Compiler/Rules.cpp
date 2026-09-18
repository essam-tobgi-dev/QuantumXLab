// Spec 14 §4.3 — rule lookup, instantiation, and the JSON form of the table.
#include "Compiler/Rules.hpp"
#include "Compiler/CircuitUtil.hpp"
#include "Core/Json.hpp"
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::compiler {
namespace {
// "pi/2", "-3*pi/4" for multiples of π/8, otherwise the shortest round-trip decimal.
std::string constantText(double v) {
    if (v == 0.0) return "0";
    const double eighths = v / (std::numbers::pi / 8.0);
    const double r = std::nearbyint(eighths);
    if (std::abs(eighths - r) < 1e-12 && std::abs(r) <= 64) {
        long num = static_cast<long>(r), den = 8;
        while (num % 2 == 0 && den > 1) { num /= 2; den /= 2; }
        std::string s = num < 0 ? "-" : "";
        if (std::labs(num) != 1) s += std::format("{}*", std::labs(num));
        s += "pi";
        if (den != 1) s += std::format("/{}", den);
        return s;
    }
    return std::format("{}", v);
}
} // namespace

double Affine::eval(std::span<const double> p) const {
    double v = c0;
    for (std::size_t k = 0; k < c.size() && k < p.size(); ++k) v += c[k] * p[k];
    return v;
}

bool Affine::zero() const {
    if (c0 != 0.0) return false;
    for (double v : c)
        if (v != 0.0) return false;
    return true;
}

std::string Affine::text() const {
    std::string s;
    for (std::size_t k = 0; k < c.size(); ++k) {
        if (c[k] == 0.0) continue;
        const double a = std::abs(c[k]);
        s += s.empty() ? (c[k] < 0 ? "-" : "") : (c[k] < 0 ? " - " : " + ");
        if (a != 1.0) s += std::format("{}*", a);
        s += std::format("p{}", k);
    }
    if (c0 != 0.0 || s.empty()) {
        const std::string k = constantText(std::abs(c0));
        if (s.empty()) s = (c0 < 0 ? "-" : "") + k;
        else s += (c0 < 0 ? " - " : " + ") + k;
    }
    return s;
}

const Rule* findRule(std::string_view source, std::string_view family) {
    const Rule* generic = nullptr;
    for (const Rule& r : decompositionRules()) {
        if (r.source != source) continue;
        if (r.family == family && family != "cx") return &r;
        if (r.family == "cx") generic = &r;
    }
    return generic;
}

const Rule* findGenericRule(std::string_view source) { return findRule(source, "cx"); }

Result<RuleExpansion> applyRule(const Rule& rule, const ir::Gate& source) {
    RuleExpansion out;
    out.phase = rule.phase.eval(source.params);
    out.gates.reserve(rule.steps.size());
    for (const RuleStep& st : rule.steps) {
        std::vector<ir::Wire> wires;
        wires.reserve(st.operands.size());
        for (std::uint8_t k : st.operands) {
            if (k >= source.targets.size())
                return fail(ErrorCode::Internal, std::format("rule '{}' uses operand {} of a {}-qubit gate", rule.source, k, source.targets.size()));
            wires.push_back(source.targets[k]);
        }
        std::vector<double> params;
        params.reserve(st.params.size());
        for (const Affine& a : st.params) params.push_back(a.eval(source.params));
        QXL_TRY_ASSIGN(ir::Gate made, gate(st.gate, std::move(wires), std::move(params), source.span));
        out.gates.push_back(std::move(made));
    }
    return out;
}

std::string decompositionRulesJson() {
    core::Json rules = core::Json::array();
    for (const Rule& r : decompositionRules()) {
        core::Json seq = core::Json::array();
        for (const RuleStep& st : r.steps) {
            core::Json params = core::Json::array();
            for (const Affine& a : st.params) params.push_back(a.text());
            seq.push_back({{"gate", std::string(st.gate)}, {"qubits", st.operands}, {"params", std::move(params)}});
        }
        rules.push_back({{"source", std::string(r.source)},
                         {"target_set", r.family == "cx" ? "1q+cx" : "1q+" + std::string(r.family)},
                         {"sequence", std::move(seq)},
                         {"phase", r.phase.text()},
                         {"cite", std::string(r.cite)}});
    }
    core::Json data{{"convention", "sequence in time order; source = exp(i*phase) * product; p<k> = k-th source parameter; "
                                   "qubits index the source operands"},
                    {"rules", std::move(rules)}};
    return core::JsonEnvelope::serialize("theory.decompositions", data) + "\n";
}

} // namespace qlab::compiler
