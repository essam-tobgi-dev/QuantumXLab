#pragma once
// Spec 19 §5 — the keyboard table, remappable in the user config. The binding set is a pure value
// (chord ↔ action) so the test can assert the defaults and a round-trip through JSON without an
// ImGui context; `pollShortcuts` (Shortcuts.cpp) is the only part that touches ImGui.
#include "Core/Error.hpp"
#include "Core/Json.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qlab::ui {

// Everything the §5 table can trigger. `None` terminates a lookup miss.
enum class Action : std::uint8_t {
    None,
    Run, Stop, Compile, StepGate, StepShot,
    WorkspaceLab, WorkspaceProgram, WorkspaceAnalysis,
    FocusSelection, ToggleXray, ToggleExploded, ToggleCutaway,
    SearchComponent, Autocomplete, GotoDefinition, FixIt,
    SaveProject, ExportResults, TogglePhysicalLab, Settings,
    Undo, Redo,
    Count
};
std::string_view actionName(Action a);          // "run", "workspace.lab", …
std::string_view actionLabel(Action a);          // strings.en.json key of the menu label
std::optional<Action> actionFromName(std::string_view name);

// A chord: a named key plus modifiers. The key name is the ImGui key spelling without the prefix
// ("F5", "1", "L", "Space", "Period"), so a binding survives a keyboard layout change.
struct Chord {
    std::string key;
    bool ctrl = false;   // Cmd on macOS: ImGui's `Ctrl` shortcut modifier is mapped to Super there
    bool shift = false;
    bool alt = false;
    bool operator==(const Chord&) const = default;

    std::string text() const;                    // "Ctrl+Shift+E"
    static Chord parse(std::string_view text);   // "" key when the text is malformed
    bool valid() const { return !key.empty(); }
};

class Shortcuts {
public:
    // The spec 19 §5 table.
    static Shortcuts defaults();

    void bind(Action a, Chord c);
    void unbind(Action a);
    const Chord* chord(Action a) const;
    std::string text(Action a) const;            // "F5", or "" when unbound
    Action lookup(const Chord& c) const;         // None when nothing is bound to it
    std::span<const std::pair<Action, Chord>> all() const { return bindings_; }

    core::Json toJson() const;
    static Result<Shortcuts> fromJson(const core::Json& j);

private:
    std::vector<std::pair<Action, Chord>> bindings_;
};

// Polls the current ImGui frame and returns every action whose chord fired this frame, in table
// order. Must be called inside a frame; returns nothing without an ImGui context.
// `textFocused` suppresses the single-letter viewport chords while a text field has focus.
std::vector<Action> pollShortcuts(const Shortcuts& map, bool textFocused);
// Camera bookmarks (spec 19 §5): digit 1–9 recalls, Ctrl+digit stores. Returns 0 when neither
// fired, +n to recall bookmark n, −n to store it.
int pollBookmarkDigit(bool textFocused);

} // namespace qlab::ui
