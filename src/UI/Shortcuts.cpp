// Spec 19 §5 — the keyboard table (see Shortcuts.hpp).
#include "UI/Shortcuts.hpp"
#include "UI/Theme.hpp"
#include <algorithm>
#include <array>
#include <imgui.h>

namespace qlab::ui {
namespace {

struct ActionInfo {
    Action action;
    std::string_view name;
    std::string_view label; // strings.en.json key
    std::string_view chord; // spec 19 §5 default
};
constexpr std::array<ActionInfo, 22> kActions{{
    {Action::Run, "run", "menu.run_program", "F5"},
    {Action::Stop, "stop", "menu.stop", "Shift+F5"},
    {Action::Compile, "compile", "menu.compile", "F6"},
    {Action::StepGate, "step.gate", "menu.step_gate", "F10"},
    {Action::StepShot, "step.shot", "menu.step_shot", "F11"},
    {Action::WorkspaceLab, "workspace.lab", "workspaces.lab", "Ctrl+1"},
    {Action::WorkspaceProgram, "workspace.program", "workspaces.program", "Ctrl+2"},
    {Action::WorkspaceAnalysis, "workspace.analysis", "workspaces.analysis", "Ctrl+3"},
    {Action::FocusSelection, "view.focus", "menu.view", "F"},
    {Action::ToggleXray, "view.xray", "menu.view", "X"},
    {Action::ToggleExploded, "view.exploded", "menu.view", "E"},
    {Action::ToggleCutaway, "view.cutaway", "menu.view", "C"},
    {Action::SearchComponent, "search.component", "menu.view", "Ctrl+P"},
    {Action::Autocomplete, "editor.autocomplete", "menu.edit", "Ctrl+Space"},
    {Action::GotoDefinition, "editor.goto_definition", "menu.edit", "F12"},
    {Action::FixIt, "editor.fixit", "menu.edit", "Ctrl+Period"},
    {Action::SaveProject, "project.save", "menu.save", "Ctrl+S"},
    {Action::ExportResults, "project.export_results", "menu.export_results", "Ctrl+Shift+E"},
    {Action::TogglePhysicalLab, "physical_lab", "app.physical_lab", "Ctrl+L"},
    {Action::Settings, "settings", "menu.edit", "Ctrl+Comma"},
    {Action::Undo, "edit.undo", "menu.undo", "Ctrl+Z"},
    {Action::Redo, "edit.redo", "menu.redo", "Ctrl+Shift+Z"},
}};

const ActionInfo* info(Action a) {
    for (const ActionInfo& e : kActions)
        if (e.action == a)
            return &e;
    return nullptr;
}

// ImGui key of a chord's key name. `ImGui::GetKeyName` is the inverse, so the table stays in step
// with whatever spelling this ImGui version uses.
ImGuiKey keyFromName(std::string_view name) {
    if (name.empty())
        return ImGuiKey_None;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const char* n = ImGui::GetKeyName(static_cast<ImGuiKey>(k));
        if (n != nullptr && name == n)
            return static_cast<ImGuiKey>(k);
    }
    return ImGuiKey_None;
}

} // namespace

std::string_view actionName(Action a) {
    const ActionInfo* i = info(a);
    return i != nullptr ? i->name : "none";
}
std::string_view actionLabel(Action a) {
    const ActionInfo* i = info(a);
    return i != nullptr ? i->label : "";
}
std::optional<Action> actionFromName(std::string_view name) {
    for (const ActionInfo& e : kActions)
        if (e.name == name)
            return e.action;
    return std::nullopt;
}

// ---------------------------------------------------------------- Chord

std::string Chord::text() const {
    std::string out;
    if (ctrl)
        out += "Ctrl+";
    if (shift)
        out += "Shift+";
    if (alt)
        out += "Alt+";
    out += key;
    return out;
}

Chord Chord::parse(std::string_view text) {
    Chord c;
    std::size_t at = 0;
    while (true) {
        const std::size_t plus = text.find('+', at);
        if (plus == std::string_view::npos)
            break;
        const std::string_view part = text.substr(at, plus - at);
        if (part == "Ctrl" || part == "Cmd" || part == "Super")
            c.ctrl = true;
        else if (part == "Shift")
            c.shift = true;
        else if (part == "Alt" || part == "Option")
            c.alt = true;
        else
            return {}; // unknown modifier: malformed
        at = plus + 1;
    }
    c.key = std::string(text.substr(at));
    if (c.key.empty())
        return {};
    return c;
}

// ---------------------------------------------------------------- Shortcuts

Shortcuts Shortcuts::defaults() {
    Shortcuts s;
    for (const ActionInfo& e : kActions)
        s.bind(e.action, Chord::parse(e.chord));
    return s;
}

void Shortcuts::bind(Action a, Chord c) {
    if (a == Action::None || !c.valid())
        return;
    for (auto& [action, chord] : bindings_)
        if (action == a) {
            chord = std::move(c);
            return;
        }
    bindings_.emplace_back(a, std::move(c));
}

void Shortcuts::unbind(Action a) {
    std::erase_if(bindings_, [a](const auto& b) { return b.first == a; });
}

const Chord* Shortcuts::chord(Action a) const {
    for (const auto& [action, c] : bindings_)
        if (action == a)
            return &c;
    return nullptr;
}

std::string Shortcuts::text(Action a) const {
    const Chord* c = chord(a);
    return c != nullptr ? c->text() : std::string{};
}

Action Shortcuts::lookup(const Chord& c) const {
    for (const auto& [action, bound] : bindings_)
        if (bound == c)
            return action;
    return Action::None;
}

core::Json Shortcuts::toJson() const {
    core::Json j = core::Json::object();
    for (const auto& [action, c] : bindings_)
        j[std::string(actionName(action))] = c.text();
    return j;
}

Result<Shortcuts> Shortcuts::fromJson(const core::Json& j) {
    if (!j.is_object())
        return fail(err::BadLayout, "shortcuts: not an object");
    Shortcuts s = defaults();
    for (const auto& [key, value] : j.items()) {
        const auto action = actionFromName(key);
        if (!action)
            return fail(err::BadLayout, "shortcuts: unknown action '" + key + "'");
        if (!value.is_string())
            return fail(err::BadLayout, "shortcuts: '" + key + "' is not a string");
        const std::string text = value.get<std::string>();
        if (text.empty()) {
            s.unbind(*action);
            continue;
        }
        const Chord c = Chord::parse(text);
        if (!c.valid())
            return fail(err::BadLayout, "shortcuts: malformed chord '" + text + "'");
        s.bind(*action, c);
    }
    return s;
}

// ---------------------------------------------------------------- polling

std::vector<Action> pollShortcuts(const Shortcuts& map, bool textFocused) {
    std::vector<Action> fired;
    if (ImGui::GetCurrentContext() == nullptr)
        return fired;
    const ImGuiIO& io = ImGui::GetIO();
    for (const ActionInfo& e : kActions) {
        const Chord* c = map.chord(e.action);
        if (c == nullptr)
            continue;
        const ImGuiKey key = keyFromName(c->key);
        if (key == ImGuiKey_None)
            continue;
        // Spec 19 §5: the bare viewport letters must not steal keystrokes from the editor.
        if (textFocused && !c->ctrl && !c->alt && c->key.size() == 1)
            continue;
        if (!ImGui::IsKeyPressed(key, false))
            continue;
        if (io.KeyShift != c->shift || io.KeyAlt != c->alt)
            continue;
        if ((io.KeyCtrl || io.KeySuper) != c->ctrl)
            continue;
        fired.push_back(e.action);
    }
    return fired;
}

int pollBookmarkDigit(bool textFocused) {
    if (ImGui::GetCurrentContext() == nullptr || textFocused)
        return 0;
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift || io.KeyAlt)
        return 0;
    for (int n = 1; n <= 9; ++n) {
        const auto key = static_cast<ImGuiKey>(ImGuiKey_0 + n);
        if (!ImGui::IsKeyPressed(key, false))
            continue;
        return (io.KeyCtrl || io.KeySuper) ? -n : n;
    }
    return 0;
}

} // namespace qlab::ui
