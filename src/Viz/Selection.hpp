#pragma once
// Spec 21 §1.1 — the one application-level selection shared by every state view, the 3D lab and
// the Inspector: selected physical qubits, selected coupling edge, selected lab component.
// Clicking a qubit anywhere selects it everywhere: the qubit ↔ component map (the transmon pad or
// ion node of each qubit, `lab::Scene::qubitNodes()`) turns a qubit selection into a component
// selection and back. Headless; the owner is the App layer, views hold a pointer.
#include "Core/StrongType.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace qlab::lab {
class Scene;
class Interaction;
} // namespace qlab::lab

namespace qlab::viz {

class SelectionModel {
  public:
    using Edge = std::pair<QubitIndex, QubitIndex>; // stored with first < second
    using Listener = std::function<void(const SelectionModel&)>;

    // ---- qubits. `additive` keeps the current selection (Shift-click); a plain click replaces it.
    // Selecting a qubit also selects its lab component when the map knows one.
    void selectQubit(QubitIndex q, bool additive = false);
    void toggleQubit(QubitIndex q);
    void setQubits(std::span<const QubitIndex> qubits);
    void clearQubits();
    std::span<const QubitIndex> qubits() const { return qubits_; } // in selection order
    bool isSelected(QubitIndex q) const;
    std::optional<QubitIndex> primaryQubit() const; // the most recently selected

    // ---- coupling edge (coupling-graph and entanglement views; the Inspector shows the coupler)
    void selectEdge(QubitIndex a, QubitIndex b);
    void clearEdge();
    const std::optional<Edge>& edge() const { return edge_; }

    // ---- lab component. A component that is a qubit's node selects that qubit as well.
    void selectComponent(ComponentId id);
    ComponentId component() const { return component_; }

    // ---- hover (transient; does not notify listeners, does not bump the revision)
    void setHoveredQubit(std::optional<QubitIndex> q) { hovered_ = q; }
    std::optional<QubitIndex> hoveredQubit() const { return hovered_; }

    // ---- basis-state focus: a click on a Q-sphere node restricts the amplitude views (spec 21
    // §3.4)
    void setBasisFocus(std::optional<std::uint64_t> index);
    std::optional<std::uint64_t> basisFocus() const { return basisFocus_; }

    void clear();

    // ---- qubit ↔ component map, read once at device load (spec 21 §1.1)
    void setQubitComponents(std::span<const ComponentId> nodePerQubit);
    void bindScene(const lab::Scene& scene); // scene.qubitNodes()
    std::optional<ComponentId> componentOf(QubitIndex q) const;
    std::optional<QubitIndex> qubitOf(ComponentId id) const;

    // ---- change tracking
    std::uint64_t revision() const { return revision_; } // bumped on every real change
    int subscribe(Listener fn);                          // returns a token
    void unsubscribe(int token);

  private:
    void changed();
    std::vector<QubitIndex> qubits_;
    std::optional<Edge> edge_;
    ComponentId component_{0};
    std::optional<QubitIndex> hovered_;
    std::optional<std::uint64_t> basisFocus_;
    std::vector<ComponentId> nodeOfQubit_;
    std::vector<std::pair<int, Listener>> listeners_;
    int nextToken_ = 1;
    std::uint64_t revision_ = 0;
};

// Keeps a SelectionModel and the lab's Interaction in step (spec 21 §4: click qubit ↔ select in
// lab). Call `sync()` once per frame on the UI thread: whichever side changed since the last call
// is copied to the other; when both changed, the lab pick wins (it is the more recent mouse event
// of that frame).
class LabSelectionBridge {
  public:
    LabSelectionBridge(SelectionModel& model, lab::Interaction& lab);
    void sync();

  private:
    SelectionModel* model_;
    lab::Interaction* lab_;
    ComponentId lastLab_{0};
    std::uint64_t lastRevision_ = 0;
};

} // namespace qlab::viz
