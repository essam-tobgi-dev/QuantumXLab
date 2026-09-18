#pragma once
// Spec 19 §3 "Code Editor" — everything the editor knows about the text, with no ImGui anywhere:
// token-kind highlighting from the LENIENT tokenizer (spec 13 §11), the diagnostics gutter and
// squiggle spans from `lang::Diagnostic`, hover documentation from `Assets/Theory/gates.json`,
// go-to-definition for user `gate`s and `defcal`s, autocomplete over `stdgates.inc`, the declared
// qubits and registers and the `pragma qlab.*` keys, bracket matching and auto-indent.
//
// The analysis is recomputed at most once per document revision (whole-buffer, O(n)); drawing then
// only touches the visible lines, which is what keeps a 100 k-line file scrolling smoothly.
#include "Lang/Diagnostics.hpp"
#include "Lang/Token.hpp"
#include "UI/Editor/Document.hpp"
#include "UI/Widgets/Equations.hpp"
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qlab::ui::editor {

// One highlighted run inside a line; `begin`/`end` are 1-based code-point columns, `end` exclusive.
struct HighlightSpan {
    std::uint32_t line = 1, begin = 1, end = 1;
    lang::TokenKind kind = lang::TokenKind::Identifier;
};

// A diagnostic projected onto the buffer: the gutter icon sits on `line`, the squiggle runs from
// `begin` to `end` on it.
struct Marker {
    std::uint32_t line = 1, begin = 1, end = 1;
    lang::Severity severity = lang::Severity::Error;
    std::string id, message, fix;
};

enum class CompletionKind : std::uint8_t { Keyword, Type, Gate, Builtin, UserGate, Defcal, Qubit, Register, Pragma };
struct Completion {
    std::string text;
    CompletionKind kind = CompletionKind::Keyword;
    std::string detail;     // signature or declaration, shown beside the entry
    bool operator==(const Completion& o) const { return text == o.text && kind == o.kind; }
};

// A user-declared `gate` or `defcal` the editor can jump to (spec 19 §5 `F12`).
struct Definition {
    std::string name;
    Position at;
    bool defcal = false;
    std::string signature;
};

class EditorModel {
public:
    Document& doc() { return doc_; }
    const Document& doc() const { return doc_; }
    void setSource(std::string text) { doc_.setText(std::move(text)); }

    // Compile/run diagnostics for this buffer (spec 14 §11 publishes them 150 ms after the edit).
    void setDiagnostics(std::vector<lang::Diagnostic> diagnostics);
    const std::vector<lang::Diagnostic>& diagnostics() const { return diagnostics_; }
    std::uint32_t errorCount() const { return errors_; }
    std::uint32_t warningCount() const { return warnings_; }
    // True while the buffer has been edited since these diagnostics were published: the compile
    // is still 150 ms behind the keystroke (spec 14 §11), so the squiggles are drawn faded.
    bool markersStale() const { return markerRevision_ != doc_.revision(); }

    // ---- analysis (all lazy, cached per document revision)
    std::span<const HighlightSpan> highlights() const;
    std::span<const HighlightSpan> highlightsOn(std::uint32_t line) const;
    std::span<const Marker> markers() const;
    std::vector<const Marker*> markersOn(std::uint32_t line) const;
    std::span<const Definition> definitions() const;
    // Identifier under the position, "" when there is none.
    std::string wordAt(Position p) const;
    std::optional<Definition> definitionOf(std::string_view name) const;
    // Spec 19 §3: hover documentation for gates and builtins.
    const GateDocEntry* hoverDoc(Position p, const TheoryAssets& assets) const;

    // ---- editing aids
    // The matching bracket of the one at (or just before) `p`; nullopt when there is none.
    std::optional<Position> matchingBracket(Position p) const;
    // Indentation the next line should start with after `line` (two spaces per open brace).
    std::string indentAfter(std::uint32_t line) const;
    // Spec 19 §5 `Ctrl+Space`: candidates for the word being typed at `p`, best first.
    std::vector<Completion> completions(Position p) const;
    // Applies a completion at `p`, replacing the partial word. Returns the caret after it.
    Position applyCompletion(Position p, const Completion& c);

private:
    void analyse() const;

    Document doc_;
    std::vector<lang::Diagnostic> diagnostics_;
    std::uint32_t errors_ = 0, warnings_ = 0;

    mutable std::uint64_t analysedRevision_ = 0;
    mutable std::vector<lang::Token> tokens_;
    mutable std::vector<HighlightSpan> highlights_;
    mutable std::vector<std::size_t> lineIndex_;      // first highlight of each line, size lines + 1
    mutable std::vector<Marker> markers_;
    mutable std::vector<Definition> definitions_;
    mutable std::vector<Completion> declared_;        // qubits, registers and user gates in scope
    mutable std::uint64_t markerRevision_ = 0;
};

// The `pragma qlab.*` keys the editor completes (spec 13 §7).
std::span<const std::string_view> pragmaKeys();
std::string_view completionKindName(CompletionKind k);

} // namespace qlab::ui::editor
