#pragma once
// Spec 17 §4/§5 — the component catalog: every Assets/Lab/Components/<id>/component.json parsed
// into an immutable descriptor, plus the `Inspectable` record the inspector panel (19 §5) renders.
#include "Core/Json.hpp"
#include "Data/Fidelity.hpp"
#include "Lab/Types.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace qlab::lab {

// Spec 17 §4 `spec_sheet[]`. `unit` is a catalog symbol (spec 05 §6); empty = dimensionless.
// `binding` keeps its `$…` placeholders; they are substituted per node (BindingRegistry::resolve).
struct SpecRow {
    std::string field;
    std::string unit;
    std::optional<std::pair<double, double>> typical;
    std::optional<std::string> binding;
    data::FidelityClass cls = data::FidelityClass::Model;
    bool simulatorOnly = false; // spec 00 §6: `device.qubit[i].bloch` and friends
    bool unitDeclared = false;  // lint: every numeric row declares `unit`
    bool classDeclared = false; // lint: every live row declares `class`
};

// Spec 17 §4 `lod[]`, sorted by ascending `max_m`.
struct LodRule {
    double maxDistance_m = 1e9;
    Detail detail = Detail::Full;
};

// Spec 17 §3.3 — a rack unit that is also an `instr::IInstrument` (spec 12): the descriptor's
// `instrument` object names the instrument class, its settings schema and its channels
// (the channel list supplies the `$k` values of `instr.*.ch[$k]` bindings).
struct InstrumentInfo {
    std::string kind;           // instrument.class, e.g. "sg_mw"
    std::string settingsSchema; // instrument.settings_schema
    std::vector<std::string> channels;
};

// One parsed component.json. Geometry parameters stay as JSON so each generator reads its own
// keys without the catalog knowing every generator's schema.
struct ComponentDescriptor {
    std::string id;
    std::string name;
    std::string category;
    std::string parent;
    std::string function;
    std::string physicsSummary;
    std::vector<std::string> equationIds;
    std::vector<SpecRow> specSheet;
    std::vector<std::string> theoryAnchors;
    std::string tooltip;
    std::string generator; // geometry.generator
    core::Json geometry;   // geometry object (generator + params)
    std::vector<LodRule> lod;
    std::string modelName; // instruments only (spec 17 §3.3)
    std::optional<InstrumentInfo> instrument;
    bool simulatorOnly = false; // any spec row is Simulator-only

    double geometryNumber(std::string_view key, double fallback) const;
    std::string geometryString(std::string_view key, std::string_view fallback) const;
    // LOD detail for a camera distance (spec 17 §10; the renderer applies the 10 % hysteresis).
    Detail detailAt(double distance_m) const;
    // First spec row with a binding, used by the hover tooltip (spec 17 §7.1).
    const SpecRow* firstLiveRow() const;
};

// Spec 17 §5 — what the inspector shows for one selected node.
struct Inspectable {
    ComponentId id{0};
    std::string instanceName;
    std::string displayName;
    std::string descriptorId;
    std::string name, category, function, physicsSummary, tooltip;
    std::vector<std::string> equationIds;
    std::vector<SpecRow> specSheet;
    std::vector<std::string> theoryAnchors;
    std::vector<ComponentId> children;
    std::vector<ComponentId> breadcrumb; // root → this node
    bool simulatorOnly = false;
    bool hasDescriptor = false; // false for grouping nodes and scenery props
};

class ComponentCatalog {
  public:
    // Loads and validates every component.json under `dir`
    // (default: core::assetDir()/"Lab"/"Components"). Every failure names the offending file.
    static Result<ComponentCatalog> load(const std::filesystem::path& dir = {});

    const ComponentDescriptor* find(std::string_view id) const;
    bool contains(std::string_view id) const { return find(id) != nullptr; }
    std::size_t size() const { return items_.size(); }
    std::vector<std::string> ids() const;
    const std::vector<ComponentDescriptor>& all() const { return items_; }

    // Lint of spec 17 §4 applied to one parsed descriptor; every problem is an Error note.
    static Status validate(const ComponentDescriptor& d);
    static Result<ComponentDescriptor> parse(const core::Json& data,
                                             const std::filesystem::path& from);

    // Asset lint (spec 25 §7): every `physics.equations` id exists in `equationsJson`
    // (Assets/Theory/equations.json) and every theory anchor `Tnn#<n>-<slug>` names a heading of
    // docs/theory/Tnn-*.md. Returns one line per unresolved reference.
    std::vector<std::string> lintReferences(const std::filesystem::path& equationsJson,
                                            const std::filesystem::path& theoryDir) const;

  private:
    std::vector<ComponentDescriptor> items_;
    std::map<std::string, std::size_t, std::less<>> byId_;
};

// Fidelity class from the descriptor's `class` string; unknown/absent → Model (spec 00 §5).
data::FidelityClass fidelityFromName(std::string_view s);

// Number of Unicode code points in UTF-8 text (tooltip limits are in characters, spec 17 §4).
std::size_t utf8Length(std::string_view s);

} // namespace qlab::lab
