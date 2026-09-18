// Spec 19 §2/§6 — the string table (see Strings.hpp).
#include "UI/Strings.hpp"
#include "Core/Paths.hpp"
#include "UI/Theme.hpp"
#include <algorithm>
#include <span>

namespace qlab::ui {
namespace {

void flatten(const core::Json& node, const std::string& prefix, std::map<std::string, std::string, std::less<>>& out) {
    if (node.is_object()) {
        for (const auto& [key, value] : node.items())
            flatten(value, prefix.empty() ? key : prefix + "." + key, out);
    } else if (node.is_string()) {
        out[prefix] = node.get<std::string>();
    } else if (!node.is_null()) {
        out[prefix] = node.dump();
    }
}

Strings& mutableGlobal() {
    static Strings s;
    return s;
}

} // namespace

Result<Strings> Strings::fromJson(const core::Json& data) {
    if (!data.is_object()) return fail(err::BadAsset, "strings: `data` is not an object");
    Strings s;
    flatten(data, {}, s.table_);
    if (s.table_.empty()) return fail(err::BadAsset, "strings: no entries");
    return s;
}

Result<Strings> Strings::load() {
    const std::filesystem::path path = core::assetDir() / "Lang" / "strings.en.json";
    QXL_TRY_ASSIGN(const core::Envelope env, core::JsonEnvelope::load(path, "ui.strings"));
    return fromJson(env.data);
}

const Strings& Strings::global() { return mutableGlobal(); }
void Strings::setGlobal(Strings s) { mutableGlobal() = std::move(s); }

bool Strings::has(std::string_view key) const { return table_.find(key) != table_.end(); }

std::string_view Strings::get(std::string_view key) const {
    const auto it = table_.find(key);
    if (it != table_.end()) return it->second;
    if (std::find(missing_.begin(), missing_.end(), key) == missing_.end()) missing_.emplace_back(key);
    return key;
}

std::string Strings::format(std::string_view key, std::span<const std::pair<std::string_view, std::string>> args) const {
    std::string out(get(key));
    for (const auto& [name, value] : args) {
        const std::string needle = "{" + std::string(name) + "}";
        for (std::size_t at = out.find(needle); at != std::string::npos; at = out.find(needle, at + value.size()))
            out.replace(at, needle.size(), value);
    }
    return out;
}

std::string_view tr(std::string_view key) { return Strings::global().get(key); }

} // namespace qlab::ui
