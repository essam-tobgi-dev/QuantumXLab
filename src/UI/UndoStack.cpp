// Spec 19 §5.3 — the undo stack (see UndoStack.hpp).
#include "UI/UndoStack.hpp"
#include <memory>
#include <string_view>
#include <utility>

namespace qlab::ui {

void UndoStack::push(Command c) {
    if (!c.undo || !c.redo) return;
    if (depth_ > 0) {
        open_.back().members.push_back(std::move(c));
        return;
    }
    Entry e;
    e.label = c.label;
    e.mergeKey = c.mergeKey;
    e.members.push_back(std::move(c));
    commit(std::move(e));
}

void UndoStack::commit(Entry e) {
    if (e.members.empty()) return;
    undone_.clear();
    // Spec 19 §5.3: a continued gesture collapses into the entry it extends.
    if (!e.mergeKey.empty() && !done_.empty() && done_.back().mergeKey == e.mergeKey) {
        Entry& into = done_.back();
        for (Command& c : e.members) into.members.push_back(std::move(c));
        return;
    }
    done_.push_back(std::move(e));
    if (done_.size() > capacity_) done_.erase(done_.begin(), done_.begin() + static_cast<std::ptrdiff_t>(done_.size() - capacity_));
}

void UndoStack::beginGroup(std::string label) {
    Entry e;
    e.label = std::move(label);
    open_.push_back(std::move(e));
    ++depth_;
}

void UndoStack::endGroup() {
    if (depth_ == 0) return;
    Entry e = std::move(open_.back());
    open_.pop_back();
    --depth_;
    if (e.members.empty()) return;
    if (depth_ > 0) {
        // A nested group becomes one command of its parent.
        auto members = std::make_shared<std::vector<Command>>(std::move(e.members));
        Command c;
        c.label = e.label;
        c.undo = [members] {
            for (auto it = members->rbegin(); it != members->rend(); ++it) it->undo();
        };
        c.redo = [members] {
            for (Command& m : *members) m.redo();
        };
        open_.back().members.push_back(std::move(c));
        return;
    }
    commit(std::move(e));
}

std::string_view UndoStack::undoLabel() const { return done_.empty() ? std::string_view{} : done_.back().label; }
std::string_view UndoStack::redoLabel() const { return undone_.empty() ? std::string_view{} : undone_.back().label; }

bool UndoStack::undo() {
    if (done_.empty()) return false;
    Entry e = std::move(done_.back());
    done_.pop_back();
    for (auto it = e.members.rbegin(); it != e.members.rend(); ++it) it->undo();
    undone_.push_back(std::move(e));
    return true;
}

bool UndoStack::redo() {
    if (undone_.empty()) return false;
    Entry e = std::move(undone_.back());
    undone_.pop_back();
    for (Command& c : e.members) c.redo();
    done_.push_back(std::move(e));
    return true;
}

void UndoStack::clear() {
    done_.clear();
    undone_.clear();
    open_.clear();
    depth_ = 0;
}

} // namespace qlab::ui
