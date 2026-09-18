#pragma once
// Spec 12 §14 — the `instrument` block of a lab component descriptor
// (Assets/Lab/Components/<id>/component.json) and its lint against the instrument model:
//   · `instrument.class` is a registry kind and `settings_schema` is that instrument's schema id;
//   · every channel pattern ("ch[k].iq": k runs over the instrument's ports) names channels the
//     instrument has, or a reading its query() serves;
//   · every `instr.*` / `cryo.*` binding of the spec sheet resolves through the registry's query;
//   · every numeric spec-sheet row corresponds to a setting or model term (spec 25 §7: a row with
//     neither is an asset-lint error).
#include "Instruments/Registry.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace qlab::instr {

struct SpecSheetRow {
    std::string field, unit;
    std::optional<std::pair<double, double>> typical;
    std::string binding; // empty when the row is static
};

struct InstrumentDescriptor {
    std::string componentId; // "mw_generator"
    std::string name, modelName;
    std::string kind;           // instrument.class
    std::string settingsSchema; // instrument.settings_schema
    std::vector<std::string> channels;
    std::vector<SpecSheetRow> specSheet;
};

// Parses the envelope's `data` object. nullopt when the component has no `instrument` block.
Result<std::optional<InstrumentDescriptor>> parseInstrumentDescriptor(const core::Json& data);
// Every component with an `instrument` block under `componentsDir`
// (default: core::assetDir()/Lab/Components), sorted by component id.
Result<std::vector<InstrumentDescriptor>> loadInstrumentDescriptors(const std::filesystem::path& componentsDir = {});

// Setting key or model term that enforces a spec-sheet row of an instrument kind, e.g.
// ("sg_mw", "output power") → "power"; nullopt when the model has nothing for it.
std::optional<std::string> specSheetTerm(std::string_view kind, std::string_view field);

// Lint of one descriptor. `registry` must hold an instance of the descriptor's kind (it is created
// when missing). Returns one line per problem; empty means the descriptor is honoured.
std::vector<std::string> lintDescriptor(const InstrumentDescriptor& descriptor, InstrumentRegistry& registry);

} // namespace qlab::instr
