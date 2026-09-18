#pragma once
// Spec 19 §2/§6 — user-visible text. Every label the panels draw comes from
// `Assets/Lang/strings.en.json` through a dotted key ("panels.viewport", "run.status_running");
// no English literal is compiled into a panel. A key that does not resolve returns the key itself
// so a missing string is visible rather than silent, and `missing()` lists them for the asset lint.
#include "Core/Error.hpp"
#include "Core/Json.hpp"
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qlab::ui {

class Strings {
public:
    // Loads Assets/Lang/strings.en.json (envelope kind "ui.strings").
    static Result<Strings> load();
    static Result<Strings> fromJson(const core::Json& data);
    // The process-wide table; empty (every key echoes itself) until `setGlobal`.
    static const Strings& global();
    static void setGlobal(Strings s);

    // Dotted path lookup; returns `key` when it does not resolve (and records it).
    std::string_view get(std::string_view key) const;
    bool has(std::string_view key) const;
    std::size_t size() const { return table_.size(); }
    // Keys asked for that the asset does not define, in first-asked order (spec 25 §7 asset lint).
    const std::vector<std::string>& missing() const { return missing_; }

    // `{n}`-style substitution: `format("run.status_running", {{"shot", "12"}, {"shots", "1024"}})`.
    std::string format(std::string_view key, std::span<const std::pair<std::string_view, std::string>> args) const;

private:
    std::map<std::string, std::string, std::less<>> table_;
    mutable std::vector<std::string> missing_;
};

// Shorthand for the global table: `tr("panels.inspector")`.
std::string_view tr(std::string_view key);

} // namespace qlab::ui
