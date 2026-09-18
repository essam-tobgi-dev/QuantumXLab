// Spec 21 §1.1 — shared selection model and its bridge to the lab (see Selection.hpp).
#include "Viz/Selection.hpp"
#include "Lab/Interaction.hpp"
#include "Lab/Scene.hpp"
#include <algorithm>

namespace qlab::viz {

void SelectionModel::changed() {
    ++revision_;
    // Listeners may unsubscribe while being called: iterate over a copy.
    const auto snapshot = listeners_;
    for (const auto& [token, fn] : snapshot)
        if (fn) fn(*this);
}

void SelectionModel::selectQubit(QubitIndex q, bool additive) {
    const bool already = isSelected(q);
    if (!additive && qubits_.size() == 1 && already) {
        // Re-selecting the only selected qubit still re-asserts its component (the lab may have moved on).
        const auto node = componentOf(q);
        if (!node || *node == component_) return;
        component_ = *node;
        changed();
        return;
    }
    if (!additive) qubits_.clear();
    else if (already) qubits_.erase(std::remove(qubits_.begin(), qubits_.end(), q), qubits_.end());
    qubits_.push_back(q); // most recent last: primaryQubit()
    if (const auto node = componentOf(q)) component_ = *node;
    changed();
}

void SelectionModel::toggleQubit(QubitIndex q) {
    if (isSelected(q)) {
        qubits_.erase(std::remove(qubits_.begin(), qubits_.end(), q), qubits_.end());
        if (const auto node = componentOf(q); node && *node == component_) {
            component_ = ComponentId{0};
            if (const auto p = primaryQubit())
                if (const auto other = componentOf(*p)) component_ = *other;
        }
        changed();
    } else {
        selectQubit(q, true);
    }
}

void SelectionModel::setQubits(std::span<const QubitIndex> qubits) {
    std::vector<QubitIndex> unique;
    for (QubitIndex q : qubits)
        if (std::find(unique.begin(), unique.end(), q) == unique.end()) unique.push_back(q);
    if (unique == qubits_) return;
    qubits_ = std::move(unique);
    if (const auto p = primaryQubit())
        if (const auto node = componentOf(*p)) component_ = *node;
    changed();
}

void SelectionModel::clearQubits() {
    if (qubits_.empty()) return;
    qubits_.clear();
    changed();
}

bool SelectionModel::isSelected(QubitIndex q) const { return std::find(qubits_.begin(), qubits_.end(), q) != qubits_.end(); }

std::optional<QubitIndex> SelectionModel::primaryQubit() const {
    return qubits_.empty() ? std::optional<QubitIndex>{} : qubits_.back();
}

void SelectionModel::selectEdge(QubitIndex a, QubitIndex b) {
    if (a == b) return;
    const Edge e{std::min(a, b), std::max(a, b)};
    if (edge_ && *edge_ == e) return;
    edge_ = e;
    changed();
}

void SelectionModel::clearEdge() {
    if (!edge_) return;
    edge_.reset();
    changed();
}

void SelectionModel::selectComponent(ComponentId id) {
    const auto q = qubitOf(id);
    const bool sameQubits = q ? (qubits_.size() == 1 && qubits_.front() == *q) : true;
    if (id == component_ && sameQubits) return;
    component_ = id;
    if (q) qubits_.assign(1, *q); // a qubit's node selects the qubit in every view
    changed();
}

void SelectionModel::setBasisFocus(std::optional<std::uint64_t> index) {
    if (index == basisFocus_) return;
    basisFocus_ = index;
    changed();
}

void SelectionModel::clear() {
    if (qubits_.empty() && !edge_ && component_ == ComponentId{0} && !basisFocus_) return;
    qubits_.clear();
    edge_.reset();
    component_ = ComponentId{0};
    basisFocus_.reset();
    changed();
}

void SelectionModel::setQubitComponents(std::span<const ComponentId> nodePerQubit) {
    nodeOfQubit_.assign(nodePerQubit.begin(), nodePerQubit.end());
}

void SelectionModel::bindScene(const lab::Scene& scene) { setQubitComponents(scene.qubitNodes()); }

std::optional<ComponentId> SelectionModel::componentOf(QubitIndex q) const {
    if (q.get() >= nodeOfQubit_.size() || nodeOfQubit_[q.get()] == ComponentId{0}) return std::nullopt;
    return nodeOfQubit_[q.get()];
}

std::optional<QubitIndex> SelectionModel::qubitOf(ComponentId id) const {
    if (id == ComponentId{0}) return std::nullopt;
    const auto it = std::find(nodeOfQubit_.begin(), nodeOfQubit_.end(), id);
    if (it == nodeOfQubit_.end()) return std::nullopt;
    return QubitIndex{static_cast<std::uint32_t>(it - nodeOfQubit_.begin())};
}

int SelectionModel::subscribe(Listener fn) {
    listeners_.emplace_back(nextToken_, std::move(fn));
    return nextToken_++;
}

void SelectionModel::unsubscribe(int token) {
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [token](const auto& l) { return l.first == token; }),
                     listeners_.end());
}

LabSelectionBridge::LabSelectionBridge(SelectionModel& model, lab::Interaction& lab)
    : model_(&model), lab_(&lab), lastLab_(lab.selected()), lastRevision_(model.revision()) {}

void LabSelectionBridge::sync() {
    const ComponentId labNow = lab_->selected();
    if (labNow != lastLab_) {
        model_->selectComponent(labNow);            // lab pick → qubit highlighted in every view
    } else if (model_->revision() != lastRevision_ && model_->component() != labNow) {
        lab_->select(model_->component());          // view click → the chip highlights the qubit's pads
    }
    lastLab_ = lab_->selected();
    lastRevision_ = model_->revision();
}

} // namespace qlab::viz
