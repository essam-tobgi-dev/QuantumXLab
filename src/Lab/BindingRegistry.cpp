// Spec 17 §5 — binding resolution.
#include "Lab/BindingRegistry.hpp"
#include "Data/Fidelity.hpp"
#include "Lab/Scene.hpp"

namespace qlab::lab {

void BindingRegistry::registerProvider(BindingRoot root, Provider p) {
    providers_[static_cast<std::size_t>(root)] = std::move(p);
}

bool BindingRegistry::hasProvider(BindingRoot root) const {
    return static_cast<bool>(providers_[static_cast<std::size_t>(root)]);
}

std::optional<BindingValue> BindingRegistry::query(BindingRoot root, std::string_view path) const {
    const auto& p = providers_[static_cast<std::size_t>(root)];
    if (!p) return std::nullopt;
    return p(path);
}

std::optional<BindingValue> BindingRegistry::resolve(std::string_view fullPath) const {
    auto split = splitBindingRoot(fullPath);
    if (!split) return std::nullopt;
    return query(split->first, split->second);
}

std::optional<BindingValue> BindingRegistry::resolve(const Binding& b) const {
    auto v = query(b.root, b.path);
    if (!v) return v;
    if (v->unit.empty()) v->unit = b.unit;
    v->cls = data::weakest(v->cls, b.cls);
    v->simulatorOnly = v->simulatorOnly || b.simulatorOnly;
    return v;
}

std::optional<BindingValue> BindingRegistry::resolve(const InstanceParams& params, const SpecRow& row) const {
    if (!row.binding) return std::nullopt;
    auto path = substitutePath(*row.binding, params);
    if (!path) return std::nullopt;
    auto split = splitBindingRoot(*path);
    if (!split) return std::nullopt;

    std::optional<BindingValue> v;
    if (split->first == BindingRoot::Static)
        if (auto n = params.number(split->second)) v = BindingValue::number(*n, row.unit, row.cls);
    if (!v) v = query(split->first, split->second);
    if (!v) return std::nullopt;
    if (v->unit.empty()) v->unit = row.unit;
    v->cls = data::weakest(v->cls, row.cls);
    v->simulatorOnly = v->simulatorOnly || row.simulatorOnly;
    return v;
}

std::optional<BindingValue> BindingRegistry::resolve(const Node& node, const SpecRow& row) const {
    return resolve(node.params, row);
}

void StaticProvider::setNumber(std::string key, double v, std::string unit, data::FidelityClass cls) {
    (*values_)[std::move(key)] = BindingValue::number(v, std::move(unit), cls);
}

std::optional<BindingValue> StaticProvider::operator()(std::string_view path) const {
    auto it = values_->find(path);
    if (it == values_->end()) return std::nullopt;
    return it->second;
}

BindingRegistry::Provider StaticProvider::asProvider() const {
    return [table = values_](std::string_view path) -> std::optional<BindingValue> {
        auto it = table->find(path);
        if (it == table->end()) return std::nullopt;
        return it->second;
    };
}

} // namespace qlab::lab
