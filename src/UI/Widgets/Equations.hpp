#pragma once
// Spec 20 §5 / 19 §6 — the equation and gate corpora the Inspector, the tooltips, the Estimates
// panel and the editor's hover documentation read from. Three assets, one loader each, all
// headless: `Assets/Theory/equations.json`, `gates.json` and `assumptions.json`.
#include "Core/Error.hpp"
#include "Core/Json.hpp"
#include "Data/Fidelity.hpp"
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::ui {

// One `terms[]` entry: the symbol as it appears in the LaTeX, what it means, and its unit.
struct EquationTerm {
    std::string symbol;   // "\\Phi", "C", "\\omega_{01}"
    std::string name;
    std::string unit;     // catalog symbol, "" when dimensionless
};

struct EquationDoc {
    std::string id;
    std::string latex;
    std::string plain;                     // ASCII form for the clipboard and CSV (spec 20 §1)
    std::vector<EquationTerm> terms;
    std::vector<std::string> assumptions;  // keys into the assumption table
    std::string theory;                    // "T05#1-the-quantized-lc-oscillator"
    data::FidelityClass cls = data::FidelityClass::Model;

    const EquationTerm* term(std::string_view symbol) const;
};

// `Assets/Theory/gates.json` — hover documentation of the code editor (spec 19 §3) and the gate
// tooltip of the circuit diagram.
struct GateDocEntry {
    std::string name, signature, description, matrixLatex, decomposition, theory;
    int qubits = 1;
    std::vector<std::string> params;
    bool clifford = false;
    std::vector<std::string> nativeFor;
};

class TheoryAssets {
public:
    // Loads all three assets. A missing file is an error naming it; an unknown field is ignored.
    static Result<TheoryAssets> load();
    static Result<TheoryAssets> fromJson(const core::Json& equations, const core::Json& gates,
                                         const core::Json& assumptions);

    const EquationDoc* equation(std::string_view id) const;
    const std::vector<EquationDoc>& equations() const { return equations_; }
    const GateDocEntry* gate(std::string_view name) const;
    const std::vector<GateDocEntry>& gates() const { return gates_; }
    // One sentence per assumption key (spec 20 §5 "the assumption list of the equation").
    std::string_view assumption(std::string_view key) const;
    std::size_t assumptionCount() const { return assumptions_.size(); }

private:
    std::vector<EquationDoc> equations_;
    std::vector<GateDocEntry> gates_;
    std::map<std::string, std::string, std::less<>> assumptions_;
    std::map<std::string, std::size_t, std::less<>> equationIndex_, gateIndex_;
};

} // namespace qlab::ui
