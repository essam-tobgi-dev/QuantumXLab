#pragma once
// Spec 17 §5 — provider registry for live values. Lab includes no Runtime or Instruments headers:
// the App registers one provider per root namespace (`cryo`, `wiring`, `device`, `instr`,
// `static`, `run`) that reads its latest immutable snapshot.
#include "Lab/Binding.hpp"
#include "Data/Fidelity.hpp"
#include "Lab/Catalog.hpp"
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace qlab::lab {

struct Node; // Scene.hpp

class BindingRegistry {
public:
    // Receives the path after "root." (placeholders substituted), e.g. "stage.mxc.T".
    // Returns nullopt when it has no value for the path.
    using Provider = std::function<std::optional<BindingValue>(std::string_view path)>;

    void registerProvider(BindingRoot root, Provider p);
    void clearProvider(BindingRoot root) { registerProvider(root, Provider{}); }
    bool hasProvider(BindingRoot root) const;

    // Raw query of one root. nullopt when no provider is registered or it has no value.
    std::optional<BindingValue> query(BindingRoot root, std::string_view path) const;
    // "cryo.stage.mxc.T" → value; nullopt for an unknown root or a missing value.
    std::optional<BindingValue> resolve(std::string_view fullPath) const;
    std::optional<BindingValue> resolve(const Binding& b) const;

    // Spec 17 §5 — one spec-sheet row of one node instance: substitutes `$line`, `$k`, `$i`, …
    // from `params`; `static.<key>` is served first from `params.numbers`, then from the static
    // provider. Unresolved placeholder, unknown root, no provider or no value → nullopt ("—").
    // The result carries the row unit when the provider gives none, the weaker of the row's and
    // the provider's fidelity class (spec 00 §5), and the Simulator-only flag of either.
    std::optional<BindingValue> resolve(const InstanceParams& params, const SpecRow& row) const;
    std::optional<BindingValue> resolve(const Node& node, const SpecRow& row) const;

private:
    std::array<Provider, kBindingRootCount> providers_;
};

// Serves `static.*`: constants from layout.json/device.json registered at scene-build time
// (spec 17 §5). Copies of the provider function share the same table, so values set after
// registration are visible.
class StaticProvider {
public:
    StaticProvider() : values_(std::make_shared<Table>()) {}
    void set(std::string key, BindingValue v) { (*values_)[std::move(key)] = std::move(v); }
    void setNumber(std::string key, double v, std::string unit = {},
                   data::FidelityClass cls = data::FidelityClass::Model);
    std::optional<BindingValue> operator()(std::string_view path) const;
    BindingRegistry::Provider asProvider() const;
    std::size_t size() const { return values_->size(); }

private:
    using Table = std::map<std::string, BindingValue, std::less<>>;
    std::shared_ptr<Table> values_;
};

} // namespace qlab::lab
