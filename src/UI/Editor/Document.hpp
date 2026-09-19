#pragma once
// Spec 19 §3 "Code Editor" — the text buffer behind the editor: one string plus a line index,
// 1-based (line, column) positions that match `SourceSpan` exactly so a `lang::Diagnostic` maps
// onto the buffer without conversion, and an edit journal for undo/redo.
//
// Columns count CODE POINTS, as the lexer's cursor does (`LexerScan.cpp`), so a diagnostic on a
// line holding non-ASCII text still lands on the right glyph.
#include "Core/Error.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::ui::editor {

struct Position {
    std::uint32_t line = 1;   // 1-based
    std::uint32_t column = 1; // 1-based, in code points
    auto operator<=>(const Position&) const = default;
};

class Document {
  public:
    Document() { setText({}); }
    explicit Document(std::string text) { setText(std::move(text)); }

    // ---- content
    void setText(std::string text); // clears the undo journal
    const std::string& text() const { return text_; }
    std::uint32_t lineCount() const { return static_cast<std::uint32_t>(lineStart_.size()); }
    std::string_view line(std::uint32_t line) const;    // without the line terminator
    std::uint32_t lineLength(std::uint32_t line) const; // in code points
    Position end() const;
    Position clamp(Position p) const;
    std::size_t offsetOf(Position p) const; // byte offset, clamped into the buffer
    Position positionOf(std::size_t offset) const;
    // Byte offset of code-point column `column` on `line` (clamped to the line end).
    std::size_t columnToByte(std::uint32_t line, std::uint32_t column) const;
    std::uint32_t byteToColumn(std::uint32_t line, std::size_t byteInLine) const;

    // ---- edits (every one is journalled)
    void insert(Position at, std::string_view what);
    void erase(Position from, Position to);
    void replace(Position from, Position to, std::string_view what);
    std::string textBetween(Position from, Position to) const;

    // ---- undo/redo (spec 19 §3; the editor's own stack, not `ui::UndoStack`, because it is
    // character-granular and merges a typing run into one entry)
    bool undo(Position* caret = nullptr);
    bool redo(Position* caret = nullptr);
    bool canUndo() const { return !done_.empty(); }
    bool canRedo() const { return !undone_.empty(); }
    void breakUndoGroup() { mergeable_ = false; }
    std::size_t undoDepth() const { return done_.size(); }

    // Bumped by every content change; the analysis caches key off it.
    std::uint64_t revision() const { return revision_; }

  private:
    struct Edit {
        std::size_t offset = 0;
        std::string removed, inserted;
        Position caretBefore, caretAfter;
    };
    void reindex();
    void apply(const Edit& e);
    void record(Edit e);

    std::string text_;
    std::vector<std::size_t> lineStart_{0};
    std::vector<Edit> done_, undone_;
    std::uint64_t revision_ = 1;
    bool mergeable_ = false;
    bool replaying_ = false;
};

// Number of code points in a UTF-8 run.
std::uint32_t codePoints(std::string_view s);

} // namespace qlab::ui::editor
