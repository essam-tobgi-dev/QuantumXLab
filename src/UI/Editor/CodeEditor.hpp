#pragma once
// Spec 19 §3 "Code Editor" — the ImGui widget over `EditorModel`: line numbers, a diagnostics
// gutter, token-kind colouring, squiggles, bracket matching, auto-indent, hover documentation,
// go-to-definition, an autocomplete popup and undo/redo.
//
// Drawing is clipped to the visible lines with `ImGuiListClipper`, so the cost per frame is
// independent of the file length (spec 19 §3: 100 k-line files scroll without hitching) and the
// analysis behind it runs at most once per edit.
#include "UI/Context.hpp"
#include "UI/Editor/EditorModel.hpp"
#include <imgui.h>
#include <string>

namespace qlab::ui::editor {

// Spec 13 §2 token kinds → theme tokens (spec 19 §1: no literal colours).
Token colorTokenFor(lang::TokenKind kind);

class CodeEditor {
  public:
    EditorModel& model() { return model_; }
    const EditorModel& model() const { return model_; }

    // Draws inside the current window, filling the available region. Returns true when the text
    // changed this frame (the panel then submits it to the incremental compiler).
    bool draw(UiContext& ctx);

    // ---- caret and navigation
    Position caret() const { return caret_; }
    void setCaret(Position p, bool select = false);
    void revealLine(std::uint32_t line);   // scrolls it into view on the next frame
    void gotoSpan(const SourceSpan& span); // Diagnostics panel → source (spec 19 §3)
    bool hasSelection() const { return selection_ != caret_; }
    std::string selectedText() const;

    // ---- commands the shortcuts table maps onto (spec 19 §5)
    void requestCompletion() { completionOpen_ = true; }
    bool gotoDefinition(); // F12; false when the word has no definition
    bool applyFixIt();     // Ctrl+. — applies the marker's `fix` at the caret
    void undo();
    void redo();

    core::Json serialize() const;
    void deserialize(const core::Json& j);

  private:
    void drawGutter(UiContext& ctx, std::uint32_t line, ImVec2 at, float gutterWidth);
    void drawLine(UiContext& ctx, std::uint32_t line, ImVec2 at, float charWidth);
    void drawCompletionPopup(UiContext& ctx, ImVec2 caretScreen, float lineHeight);
    bool handleKeys(UiContext& ctx);
    void insertText(std::string_view what);
    void deleteSelection();
    Position positionAt(ImVec2 local, float charWidth, float lineHeight) const;

    EditorModel model_;
    Position caret_{1, 1}, selection_{1, 1};
    std::uint32_t revealLine_ = 0;
    bool completionOpen_ = false;
    int completionIndex_ = 0;
    std::vector<Completion> completions_;
    bool focused_ = false;
};

} // namespace qlab::ui::editor
