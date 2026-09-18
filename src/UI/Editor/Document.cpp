// Spec 19 §3 — the editor text buffer (see Document.hpp).
#include "UI/Editor/Document.hpp"
#include <algorithm>

namespace qlab::ui::editor {
namespace {

bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

} // namespace

std::uint32_t codePoints(std::string_view s) {
    std::uint32_t n = 0;
    for (char c : s)
        if (!isContinuation(c)) ++n;
    return n;
}

void Document::setText(std::string text) {
    text_ = std::move(text);
    reindex();
    done_.clear();
    undone_.clear();
    mergeable_ = false;
    ++revision_;
}

void Document::reindex() {
    lineStart_.clear();
    lineStart_.push_back(0);
    for (std::size_t i = 0; i < text_.size(); ++i)
        if (text_[i] == '\n') lineStart_.push_back(i + 1);
}

std::string_view Document::line(std::uint32_t line) const {
    if (line == 0 || line > lineCount()) return {};
    const std::size_t begin = lineStart_[line - 1];
    const std::size_t end = line < lineCount() ? lineStart_[line] - 1 : text_.size();
    return std::string_view(text_).substr(begin, end - begin);
}

std::uint32_t Document::lineLength(std::uint32_t l) const { return codePoints(line(l)); }

Position Document::end() const {
    const std::uint32_t last = lineCount();
    return Position{last, lineLength(last) + 1};
}

Position Document::clamp(Position p) const {
    Position c = p;
    c.line = std::clamp<std::uint32_t>(c.line, 1, lineCount());
    c.column = std::clamp<std::uint32_t>(c.column, 1, lineLength(c.line) + 1);
    return c;
}

std::size_t Document::columnToByte(std::uint32_t l, std::uint32_t column) const {
    const std::string_view s = line(l);
    std::uint32_t seen = 1;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (isContinuation(s[i])) continue;
        if (seen == column) return i;
        ++seen;
    }
    return s.size();
}

std::uint32_t Document::byteToColumn(std::uint32_t l, std::size_t byteInLine) const {
    const std::string_view s = line(l);
    std::uint32_t column = 1;
    for (std::size_t i = 0; i < s.size() && i < byteInLine; ++i)
        if (!isContinuation(s[i])) ++column;
    return column;
}

std::size_t Document::offsetOf(Position p) const {
    const Position c = clamp(p);
    return lineStart_[c.line - 1] + columnToByte(c.line, c.column);
}

Position Document::positionOf(std::size_t offset) const {
    const std::size_t at = std::min(offset, text_.size());
    const auto it = std::upper_bound(lineStart_.begin(), lineStart_.end(), at);
    const auto l = static_cast<std::uint32_t>(std::distance(lineStart_.begin(), it));
    return Position{l, byteToColumn(l, at - lineStart_[l - 1])};
}

std::string Document::textBetween(Position from, Position to) const {
    std::size_t a = offsetOf(from), b = offsetOf(to);
    if (a > b) std::swap(a, b);
    return text_.substr(a, b - a);
}

void Document::apply(const Edit& e) {
    text_.replace(e.offset, e.removed.size(), e.inserted);
    reindex();
    ++revision_;
}

void Document::record(Edit e) {
    if (replaying_) return;
    undone_.clear();
    // Merge a typing run: consecutive single-character inserts at the caret become one entry.
    if (mergeable_ && !done_.empty() && e.removed.empty() && e.inserted.size() == 1 && e.inserted[0] != '\n') {
        Edit& back = done_.back();
        if (back.removed.empty() && back.offset + back.inserted.size() == e.offset) {
            back.inserted += e.inserted;
            back.caretAfter = e.caretAfter;
            return;
        }
    }
    // Enter ends the typing run: undo removes the text typed on the new line, not the line break.
    const bool newline = e.inserted.find('\n') != std::string::npos;
    done_.push_back(std::move(e));
    mergeable_ = !newline;
}

void Document::insert(Position at, std::string_view what) {
    if (what.empty()) return;
    Edit e;
    e.offset = offsetOf(at);
    e.inserted = std::string(what);
    e.caretBefore = clamp(at);
    apply(e);
    e.caretAfter = positionOf(e.offset + e.inserted.size());
    record(std::move(e));
}

void Document::erase(Position from, Position to) {
    std::size_t a = offsetOf(from), b = offsetOf(to);
    if (a > b) std::swap(a, b);
    if (a == b) return;
    Edit e;
    e.offset = a;
    e.removed = text_.substr(a, b - a);
    e.caretBefore = positionOf(b);
    apply(e);
    e.caretAfter = positionOf(a);
    record(std::move(e));
    mergeable_ = false;
}

void Document::replace(Position from, Position to, std::string_view what) {
    std::size_t a = offsetOf(from), b = offsetOf(to);
    if (a > b) std::swap(a, b);
    Edit e;
    e.offset = a;
    e.removed = text_.substr(a, b - a);
    e.inserted = std::string(what);
    // An edit that changes nothing must not land on the undo stack.
    if (e.removed == e.inserted) return;
    e.caretBefore = positionOf(b);
    apply(e);
    e.caretAfter = positionOf(a + e.inserted.size());
    record(std::move(e));
    mergeable_ = false;
}

bool Document::undo(Position* caret) {
    if (done_.empty()) return false;
    Edit e = done_.back();
    done_.pop_back();
    replaying_ = true;
    Edit inverse = e;
    std::swap(inverse.removed, inverse.inserted);
    apply(inverse);
    replaying_ = false;
    if (caret != nullptr) *caret = clamp(e.caretBefore);
    undone_.push_back(std::move(e));
    mergeable_ = false;
    return true;
}

bool Document::redo(Position* caret) {
    if (undone_.empty()) return false;
    Edit e = undone_.back();
    undone_.pop_back();
    replaying_ = true;
    apply(e);
    replaying_ = false;
    if (caret != nullptr) *caret = clamp(e.caretAfter);
    done_.push_back(std::move(e));
    mergeable_ = false;
    return true;
}

} // namespace qlab::ui::editor
