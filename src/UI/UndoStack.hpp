#pragma once
// Spec 19 §5.3 — "every edit goes through ui::UndoStack (Ctrl+Z / Ctrl+Shift+Z, 200 entries,
// grouped by drag gesture)". A command is a label plus the two closures that move the model between
// its old and new state; the stack owns nothing else, so panels stay stateless (spec 19 §3).
//
// Grouping: `beginGroup("Drag shots")` … `endGroup()` folds every command pushed in between into a
// single entry whose undo replays the members in reverse. A drag gesture opens the group on the
// first mouse-down and closes it on release, so one drag is one undo step.
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::ui {

struct Command {
    std::string label;          // shown in the Edit menu: "Undo shots"
    std::function<void()> undo;
    std::function<void()> redo;
    // Two consecutive commands with the same non-empty `mergeKey` collapse into one entry: the
    // first one's `undo` with the last one's `redo` (a slider dragged without a mouse-up group).
    std::string mergeKey;
};

class UndoStack {
public:
    static constexpr std::size_t kDefaultCapacity = 200; // spec 19 §5.3

    explicit UndoStack(std::size_t capacity = kDefaultCapacity) : capacity_(capacity == 0 ? 1 : capacity) {}

    // Pushes an already-applied edit (the caller changed the model, then records how to reverse it).
    // Clears the redo branch. A command without both closures is ignored.
    void push(Command c);
    // Convenience for the common "one value changed" case.
    template <class T> void pushValue(std::string label, T* target, T oldValue, T newValue, std::string mergeKey = {}) {
        if (target == nullptr || oldValue == newValue) return;
        push(Command{std::move(label), [target, oldValue] { *target = oldValue; },
                     [target, newValue] { *target = newValue; }, std::move(mergeKey)});
    }

    void beginGroup(std::string label);
    void endGroup();
    bool grouping() const { return depth_ > 0; }

    bool canUndo() const { return !done_.empty(); }
    bool canRedo() const { return !undone_.empty(); }
    // Labels for the Edit menu; empty when there is nothing to undo / redo.
    std::string_view undoLabel() const;
    std::string_view redoLabel() const;
    bool undo();
    bool redo();
    void clear();

    std::size_t size() const { return done_.size(); }
    std::size_t redoSize() const { return undone_.size(); }
    std::size_t capacity() const { return capacity_; }

private:
    struct Entry {
        std::string label;
        std::vector<Command> members; // applied in order; undone in reverse
        std::string mergeKey;
    };
    void commit(Entry e);
    std::size_t capacity_;
    std::vector<Entry> done_, undone_;
    std::vector<Entry> open_;   // nested groups, innermost last
    int depth_ = 0;
};

} // namespace qlab::ui
