// Spec 20 §5 / 19 §6 — the theory corpora (see Equations.hpp).
#include "UI/Widgets/Equations.hpp"
#include "Core/Paths.hpp"
#include "Lab/Catalog.hpp"
#include "UI/Theme.hpp"

namespace qlab::ui {
namespace {

std::string str(const core::Json& j, std::string_view key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}
std::vector<std::string> strArray(const core::Json& j, std::string_view key) {
    std::vector<std::string> out;
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array()) return out;
    for (const core::Json& e : *it)
        if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
}

} // namespace

const EquationTerm* EquationDoc::term(std::string_view symbol) const {
    for (const EquationTerm& t : terms)
        if (t.symbol == symbol) return &t;
    return nullptr;
}

const EquationDoc* TheoryAssets::equation(std::string_view id) const {
    const auto it = equationIndex_.find(id);
    return it == equationIndex_.end() ? nullptr : &equations_[it->second];
}

const GateDocEntry* TheoryAssets::gate(std::string_view name) const {
    const auto it = gateIndex_.find(name);
    return it == gateIndex_.end() ? nullptr : &gates_[it->second];
}

std::string_view TheoryAssets::assumption(std::string_view key) const {
    const auto it = assumptions_.find(key);
    return it == assumptions_.end() ? std::string_view{} : std::string_view(it->second);
}

Result<TheoryAssets> TheoryAssets::fromJson(const core::Json& equations, const core::Json& gates,
                                            const core::Json& assumptions) {
    TheoryAssets a;
    const auto eqs = equations.find("equations");
    if (eqs == equations.end() || !eqs->is_array()) return fail(err::BadAsset, "equations.json: missing `data.equations`");
    for (const core::Json& e : *eqs) {
        EquationDoc d;
        d.id = str(e, "id");
        if (d.id.empty()) return fail(err::BadAsset, "equations.json: an entry has no `id`");
        d.latex = str(e, "latex");
        d.plain = str(e, "plain");
        d.theory = str(e, "theory");
        d.cls = lab::fidelityFromName(str(e, "class"));
        d.assumptions = strArray(e, "assumptions");
        if (const auto terms = e.find("terms"); terms != e.end() && terms->is_array())
            for (const core::Json& t : *terms)
                d.terms.push_back(EquationTerm{str(t, "symbol"), str(t, "name"), str(t, "unit")});
        a.equationIndex_[d.id] = a.equations_.size();
        a.equations_.push_back(std::move(d));
    }

    const auto gs = gates.find("gates");
    if (gs == gates.end() || !gs->is_array()) return fail(err::BadAsset, "gates.json: missing `data.gates`");
    for (const core::Json& g : *gs) {
        GateDocEntry d;
        d.name = str(g, "name");
        if (d.name.empty()) return fail(err::BadAsset, "gates.json: an entry has no `name`");
        d.signature = str(g, "signature");
        d.description = str(g, "description");
        d.matrixLatex = str(g, "matrix_latex");
        d.decomposition = str(g, "decomposition");
        d.theory = str(g, "theory");
        d.params = strArray(g, "params");
        d.nativeFor = strArray(g, "native_for");
        if (const auto q = g.find("qubits"); q != g.end() && q->is_number_integer()) d.qubits = q->get<int>();
        if (const auto c = g.find("clifford"); c != g.end() && c->is_boolean()) d.clifford = c->get<bool>();
        a.gateIndex_[d.name] = a.gates_.size();
        a.gates_.push_back(std::move(d));
    }

    const auto as = assumptions.find("assumptions");
    if (as == assumptions.end() || !as->is_object())
        return fail(err::BadAsset, "assumptions.json: missing `data.assumptions`");
    for (const auto& [key, value] : as->items())
        if (value.is_string()) a.assumptions_[key] = value.get<std::string>();
    return a;
}

Result<TheoryAssets> TheoryAssets::load() {
    const std::filesystem::path dir = core::assetDir() / "Theory";
    QXL_TRY_ASSIGN(const core::Envelope eq, core::JsonEnvelope::load(dir / "equations.json", "theory.equations"));
    QXL_TRY_ASSIGN(const core::Envelope ga, core::JsonEnvelope::load(dir / "gates.json", "theory.gates"));
    QXL_TRY_ASSIGN(const core::Envelope au, core::JsonEnvelope::load(dir / "assumptions.json", "theory.assumptions"));
    return fromJson(eq.data, ga.data, au.data);
}

} // namespace qlab::ui
