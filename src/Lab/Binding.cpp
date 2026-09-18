// Spec 17 §5 — binding path helpers.
#include "Lab/Binding.hpp"
#include <array>
#include <cctype>
#include <format>

namespace qlab::lab {

namespace {
constexpr std::array<std::pair<BindingRoot, std::string_view>, kBindingRootCount> kRoots{{
    {BindingRoot::Cryo, "cryo"},
    {BindingRoot::Wiring, "wiring"},
    {BindingRoot::Device, "device"},
    {BindingRoot::Instr, "instr"},
    {BindingRoot::Static, "static"},
    {BindingRoot::Run, "run"},
}};

bool placeholderChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}
} // namespace

std::string_view bindingRootName(BindingRoot r) {
    for (const auto& [k, n] : kRoots)
        if (k == r) return n;
    return "?";
}

std::optional<BindingRoot> bindingRootFromName(std::string_view s) {
    for (const auto& [k, n] : kRoots)
        if (n == s) return k;
    return std::nullopt;
}

std::string Binding::fullPath() const { return std::format("{}.{}", bindingRootName(root), path); }

std::optional<std::string> substitutePath(std::string_view path, const InstanceParams& p) {
    std::string out;
    out.reserve(path.size());
    for (std::size_t i = 0; i < path.size();) {
        if (path[i] != '$') {
            out.push_back(path[i++]);
            continue;
        }
        std::size_t j = i + 1;
        while (j < path.size() && placeholderChar(path[j])) ++j;
        std::string_view key = path.substr(i + 1, j - i - 1);
        if (key.empty()) return std::nullopt;
        const std::string* v = p.token(key);
        if (!v || v->empty()) return std::nullopt;
        out += *v;
        i = j;
    }
    return out;
}

Result<std::pair<BindingRoot, std::string>> splitBindingRoot(std::string_view full) {
    std::size_t dot = full.find('.');
    if (dot == std::string_view::npos || dot + 1 >= full.size())
        return fail(kErrBinding, std::format("binding '{}' has no root namespace", full));
    auto root = bindingRootFromName(full.substr(0, dot));
    if (!root)
        return fail(kErrBinding,
                    std::format("binding '{}': unknown root '{}'", full, full.substr(0, dot)));
    return std::make_pair(*root, std::string(full.substr(dot + 1)));
}

bool isSimulatorOnlyPath(std::string_view path) {
    for (std::string_view key : {".bloch", ".purity", ".statevector", ".entropy"}) {
        std::size_t at = path.find(key);
        while (at != std::string_view::npos) {
            std::size_t end = at + key.size();
            if (end == path.size() || !placeholderChar(path[end])) return true;
            at = path.find(key, end);
        }
    }
    return false;
}

std::string formatBindingValue(const std::optional<BindingValue>& v, std::string_view unitFallback) {
    if (!v || !v->available()) return "—";
    if (const std::string* s = v->asText()) return *s;
    std::string_view unit = v->unit.empty() ? unitFallback : std::string_view(v->unit);
    std::string num = std::format("{:.4g}", v->asNumber());
    return unit.empty() ? num : std::format("{} {}", num, unit);
}

} // namespace qlab::lab
