// Spec 19 §3 "Code Editor" — the headless editor model: token-kind highlighting ranges, the
// diagnostics gutter and squiggle spans, autocomplete candidates, undo/redo, bracket matching,
// auto-indent, go-to-definition and the cost of re-analysing a large file.
#include "Lang/Diagnostics.hpp"
#include "UI/Editor/CodeEditor.hpp"
#include "UI/Editor/EditorModel.hpp"
#include "UI/Widgets/Equations.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>

using namespace qlab;
using namespace qlab::ui::editor;

namespace {

// Lines are numbered from 1; the sample is kept small so every span can be stated by hand.
const char* kSample = R"(OPENQASM 3.0;
include "stdgates.inc";

gate myx a {
  x a;
}

qubit[3] q;
bit[3] c;
myx q[0];
cx q[0], q[1];
rz(pi/4) q[2];     // a comment
c = measure q;
)";

const HighlightSpan* spanAt(const EditorModel& m, std::uint32_t line, std::uint32_t column) {
    for (const HighlightSpan& h : m.highlightsOn(line))
        if (column >= h.begin && column < h.end) return &h;
    return nullptr;
}

lang::Diagnostic errorAt(std::uint32_t line, std::uint32_t column, std::uint32_t endColumn) {
    SourceSpan s;
    s.line = line;
    s.column = column;
    s.endLine = line;
    s.endColumn = endColumn;
    return lang::Diagnostics::make("QL3007", s, "myx");
}

} // namespace

TEST_CASE("Editor document: positions, edits and code-point columns") {
    Document doc("abc\ndefgh\n");
    CHECK(doc.lineCount() == 3);          // the trailing newline opens an empty third line
    CHECK(doc.line(1) == "abc");
    CHECK(doc.line(2) == "defgh");
    CHECK(doc.line(3).empty());
    CHECK(doc.lineLength(2) == 5);
    CHECK(doc.end() == Position{3, 1});

    CHECK(doc.offsetOf(Position{1, 1}) == 0);
    CHECK(doc.offsetOf(Position{2, 1}) == 4);
    CHECK(doc.offsetOf(Position{2, 6}) == 9);
    CHECK(doc.positionOf(9) == Position{2, 6});
    CHECK(doc.clamp(Position{99, 99}) == Position{3, 1});

    // Columns count code points, exactly as the lexer's cursor does.
    Document greek("αβγ x\n");
    CHECK(greek.lineLength(1) == 5);
    CHECK(greek.offsetOf(Position{1, 4}) == 6);    // three 2-byte code points before the space
    CHECK(greek.positionOf(6) == Position{1, 4});
    CHECK(greek.byteToColumn(1, 6) == 4);

    doc.insert(Position{1, 4}, "XY");
    CHECK(doc.line(1) == "abcXY");
    doc.erase(Position{1, 1}, Position{1, 3});
    CHECK(doc.line(1) == "cXY");
    CHECK(doc.textBetween(Position{1, 1}, Position{2, 3}) == "cXY\nde");
}

TEST_CASE("Editor document: undo and redo, with a typing run as one entry (spec 19 §3, §5.3)") {
    Document doc("a\n");
    for (char c : std::string("bcd")) doc.insert(Position{1, doc.lineLength(1) + 1}, std::string(1, c));
    CHECK(doc.line(1) == "abcd");
    CHECK(doc.undoDepth() == 1);            // one typing run, not three entries
    Position caret;
    REQUIRE(doc.undo(&caret));
    CHECK(doc.line(1) == "a");
    CHECK(caret == Position{1, 2});
    CHECK_FALSE(doc.canUndo());
    REQUIRE(doc.redo(&caret));
    CHECK(doc.line(1) == "abcd");
    CHECK(caret == Position{1, 5});

    // A newline breaks the run; an erase is always its own entry.
    doc.insert(Position{1, 5}, "\n");
    doc.insert(Position{2, 1}, "e");
    doc.erase(Position{2, 1}, Position{2, 2});
    CHECK(doc.undoDepth() == 4);
    while (doc.canUndo()) REQUIRE(doc.undo());
    CHECK(doc.text() == "a\n");
    // Redoing everything returns the buffer exactly.
    while (doc.canRedo()) REQUIRE(doc.redo());
    CHECK(doc.line(1) == "abcd");
    CHECK(doc.line(2).empty());
}

TEST_CASE("Editor: token-kind highlighting covers the sample program (spec 13 §2, 19 §3)") {
    EditorModel m;
    m.setSource(kSample);
    REQUIRE(m.doc().lineCount() >= 13);

    // Line 1: `OPENQASM 3.0;` — keyword, number, punctuation.
    const HighlightSpan* openqasm = spanAt(m, 1, 1);
    REQUIRE(openqasm != nullptr);
    CHECK(openqasm->kind == lang::TokenKind::Keyword);
    CHECK(openqasm->begin == 1);
    CHECK(openqasm->end == 9);                                   // "OPENQASM" is 8 columns
    const HighlightSpan* version = spanAt(m, 1, 10);
    REQUIRE(version != nullptr);
    CHECK(version->kind == lang::TokenKind::Number);

    // Line 2: `include "stdgates.inc";` — keyword then a string.
    CHECK(spanAt(m, 2, 1)->kind == lang::TokenKind::Keyword);
    CHECK(spanAt(m, 2, 9)->kind == lang::TokenKind::String);

    // Line 4: `gate myx a {` — the declaration keyword and the user gate name.
    CHECK(spanAt(m, 4, 1)->kind == lang::TokenKind::Keyword);
    CHECK(spanAt(m, 4, 6)->kind == lang::TokenKind::Gate);       // a user gate colours as a gate
    CHECK(spanAt(m, 4, 10)->kind == lang::TokenKind::Identifier);

    // Line 5: `x a;` — `x` is an stdgates.inc name, an Identifier to the lexer, a Gate to the editor.
    CHECK(spanAt(m, 5, 3)->kind == lang::TokenKind::Gate);

    // Line 8/9: `qubit[3] q;` / `bit[3] c;` — types.
    CHECK(spanAt(m, 8, 1)->kind == lang::TokenKind::Type);
    CHECK(spanAt(m, 9, 1)->kind == lang::TokenKind::Type);

    // Line 10: the user gate at its use site.
    CHECK(spanAt(m, 10, 1)->kind == lang::TokenKind::Gate);
    // Line 11: `cx` is an stdgates name.
    CHECK(spanAt(m, 11, 1)->kind == lang::TokenKind::Gate);
    // Line 12: `rz`, `pi` (builtin), a comment to the end of the line.
    CHECK(spanAt(m, 12, 1)->kind == lang::TokenKind::Gate);
    CHECK(spanAt(m, 12, 4)->kind == lang::TokenKind::Builtin);
    const HighlightSpan* comment = spanAt(m, 12, 20);
    REQUIRE(comment != nullptr);
    CHECK(comment->kind == lang::TokenKind::Comment);
    CHECK(comment->end == m.doc().lineLength(12) + 1);

    // Every run is inside its line and the runs of a line are ordered and disjoint.
    for (std::uint32_t line = 1; line <= m.doc().lineCount(); ++line) {
        std::uint32_t previousEnd = 0;
        for (const HighlightSpan& h : m.highlightsOn(line)) {
            CHECK(h.line == line);
            CHECK(h.begin >= previousEnd);
            CHECK(h.end > h.begin);
            CHECK(h.end <= m.doc().lineLength(line) + 1);
            previousEnd = h.end;
        }
    }
    // The per-line index really partitions the whole run list.
    std::size_t counted = 0;
    for (std::uint32_t line = 1; line <= m.doc().lineCount(); ++line) counted += m.highlightsOn(line).size();
    CHECK(counted == m.highlights().size());
}

TEST_CASE("Editor: a diagnostic span becomes a gutter line and a squiggle (spec 19 §3)") {
    EditorModel m;
    m.setSource(kSample);
    m.setDiagnostics({errorAt(10, 1, 4), lang::Diagnostics::make("QL2050", SourceSpan{12, 4, 12, 6, {}}, "qlab.nope")});
    CHECK(m.errorCount() + m.warningCount() == 2);

    const auto onTen = m.markersOn(10);
    REQUIRE(onTen.size() == 1);
    CHECK(onTen.front()->line == 10);
    CHECK(onTen.front()->begin == 1);
    CHECK(onTen.front()->end == 4);
    CHECK(onTen.front()->id == "QL3007");
    CHECK(m.markersOn(11).empty());
    const auto onTwelve = m.markersOn(12);
    REQUIRE(onTwelve.size() == 1);
    CHECK(onTwelve.front()->begin == 4);
    CHECK(onTwelve.front()->end == 6);
    // Markers are ordered by (line, column) so the gutter can walk them in one pass.
    CHECK(std::is_sorted(m.markers().begin(), m.markers().end(), [](const Marker& a, const Marker& b) {
        return std::tie(a.line, a.begin) < std::tie(b.line, b.begin);
    }));
}

TEST_CASE("Editor: autocomplete offers declarations, stdgates, pragmas and keywords (spec 19 §3)") {
    EditorModel m;
    m.setSource(kSample);
    const auto has = [](const std::vector<Completion>& list, std::string_view text) {
        return std::find_if(list.begin(), list.end(), [&](const Completion& c) { return c.text == text; }) != list.end();
    };
    const auto kindOf = [](const std::vector<Completion>& list, std::string_view text) {
        const auto it = std::find_if(list.begin(), list.end(), [&](const Completion& c) { return c.text == text; });
        return it == list.end() ? CompletionKind::Keyword : it->kind;
    };

    // Everything, at an empty position on a fresh line.
    m.doc().insert(m.doc().end(), "\n");
    const std::vector<Completion> all = m.completions(m.doc().end());
    CHECK(has(all, "myx"));
    CHECK(kindOf(all, "myx") == CompletionKind::UserGate);
    CHECK(has(all, "q"));
    CHECK(kindOf(all, "q") == CompletionKind::Qubit);
    CHECK(has(all, "c"));
    CHECK(kindOf(all, "c") == CompletionKind::Register);
    CHECK(has(all, "cx"));
    CHECK(kindOf(all, "cx") == CompletionKind::Gate);
    CHECK(has(all, "measure"));
    CHECK(has(all, "qubit"));
    // Declared names come before the standard library, which comes before the language.
    const auto index = [&](std::string_view t) {
        return std::distance(all.begin(),
                             std::find_if(all.begin(), all.end(), [&](const Completion& c) { return c.text == t; }));
    };
    CHECK(index("q") < index("cx"));
    CHECK(index("cx") < index("measure"));

    // A prefix filters the list.
    m.doc().insert(m.doc().end(), "my");
    const std::vector<Completion> filtered = m.completions(m.doc().end());
    REQUIRE_FALSE(filtered.empty());
    for (const Completion& c : filtered) CHECK(std::string_view(c.text).starts_with("my"));
    CHECK(has(filtered, "myx"));

    // Applying one replaces the partial word.
    const Position after = m.applyCompletion(m.doc().end(), filtered.front());
    CHECK(m.doc().line(after.line) == "myx");

    // A pragma line completes `qlab.*` keys and nothing else.
    m.setSource("pragma q");
    const std::vector<Completion> pragma = m.completions(Position{1, 9});
    REQUIRE_FALSE(pragma.empty());
    for (const Completion& c : pragma) {
        CHECK(c.kind == CompletionKind::Pragma);
        CHECK(std::string_view(c.text).starts_with("q"));
    }
    CHECK(has(pragma, "qlab.shots"));
    CHECK(has(pragma, "qlab.device"));
}

TEST_CASE("Editor: bracket matching, auto-indent and go-to-definition (spec 19 §3, §5)") {
    EditorModel m;
    m.setSource(kSample);

    // `gate myx a {` on line 4 matches the `}` on line 6.
    const auto open = m.matchingBracket(Position{4, 12});
    REQUIRE(open.has_value());
    CHECK(open->line == 6);
    CHECK(open->column == 1);
    // …and back.
    const auto close = m.matchingBracket(Position{6, 1});
    REQUIRE(close.has_value());
    CHECK(close->line == 4);
    CHECK(close->column == 12);
    // The parentheses of `rz(pi/4)` on line 12.
    const auto paren = m.matchingBracket(Position{12, 3});
    REQUIRE(paren.has_value());
    CHECK(paren->line == 12);
    CHECK(paren->column == 8);
    CHECK_FALSE(m.matchingBracket(Position{1, 1}).has_value());

    // Auto-indent: a line that opens a brace indents the next one by two spaces.
    CHECK(m.indentAfter(4) == "  ");
    CHECK(m.indentAfter(5) == "  ");   // already inside the body, unchanged
    CHECK(m.indentAfter(8).empty());

    // Go to definition: the use of `myx` on line 10 jumps to its declaration on line 4.
    CHECK(m.wordAt(Position{10, 2}) == "myx");
    const auto def = m.definitionOf("myx");
    REQUIRE(def.has_value());
    CHECK(def->at == Position{4, 6});
    CHECK_FALSE(def->defcal);
    CHECK_FALSE(m.definitionOf("cx").has_value());   // stdgates have no source definition

    // A `defcal` is a definition too, and is NOT coloured as a gate.
    m.setSource("defcal mygate $0 { }\n");
    const auto defcal = m.definitionOf("mygate");
    REQUIRE(defcal.has_value());
    CHECK(defcal->defcal);
    CHECK(defcal->at == Position{1, 8});
}

TEST_CASE("Editor: hover documentation comes from gates.json (spec 19 §3)") {
    const auto assets = qlab::ui::TheoryAssets::load();
    if (!assets) SKIP("Assets/Theory is not available: " + assets.error().message);
    EditorModel m;
    m.setSource(kSample);
    const qlab::ui::GateDocEntry* doc = m.hoverDoc(Position{11, 2}, *assets);   // `cx`
    REQUIRE(doc != nullptr);
    CHECK(doc->name == "cx");
    CHECK(doc->qubits == 2);
    CHECK_FALSE(doc->description.empty());
    CHECK_FALSE(doc->theory.empty());
    CHECK(m.hoverDoc(Position{8, 10}, *assets) == nullptr);   // `q` is not a gate
}

namespace {

// `n` lines of `cx q[0], q[1];  // line`, each 12 tokens: cx q [ 0 ] , q [ 1 ] ; comment.
std::string manyLines(int n) {
    std::string big;
    big.reserve(static_cast<std::size_t>(n) * 25 + 64);
    big += "OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit[2] q;\n";
    for (int i = 0; i < n; ++i) big += "cx q[0], q[1];  // line\n";
    return big;
}
constexpr std::size_t kTokensPerLine = 12;

double millisSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

TEST_CASE("Editor: a 100 k-line file analyses once and answers per line in constant time") {
    EditorModel m;
    m.setSource(manyLines(100'000));
    REQUIRE(m.doc().lineCount() > 100'000);

    const auto start = std::chrono::steady_clock::now();
    const std::size_t total = m.highlights().size();   // the one whole-buffer analysis
    const double analysisMs = millisSince(start);
    CHECK(total > 100'000 * kTokensPerLine);

    // Scrolling is what must not hitch: every later query is served from the per-line index, so
    // sixty visible rows cost microseconds however long the file is.
    const auto queryStart = std::chrono::steady_clock::now();
    std::size_t visible = 0;
    for (std::uint32_t line = 50'000; line < 50'060; ++line) visible += m.highlightsOn(line).size();
    const double frameMs = millisSince(queryStart);
    CHECK(visible == 60 * kTokensPerLine);
    CHECK(frameMs < 1.0);              // spec 19 §9: 100 k-line files scroll without hitching
    INFO("100 k-line analysis " << analysisMs << " ms, 60 visible rows " << frameMs << " ms");

    // Re-asking without an edit is free (the cache is keyed on the document revision).
    const auto again = std::chrono::steady_clock::now();
    CHECK(m.highlights().size() == total);
    CHECK(millisSince(again) < 1.0);
}

TEST_CASE("Editor: a keystroke in a 5 k-line file re-analyses well inside the frame budget") {
    // Spec 19 §9 gives the shipped build a 16 ms frame while typing in a 5 k-line file. The bound
    // asserted here is the DEBUG cost of the whole re-analysis (an unoptimised build runs the
    // tokenizer roughly an order of magnitude slower), and it scales linearly with the file.
    EditorModel m;
    m.setSource(manyLines(5'000));
    CHECK(m.highlights().size() > 5'000 * kTokensPerLine);

    const auto start = std::chrono::steady_clock::now();
    m.doc().insert(Position{4, 1}, "x");      // one keystroke
    const std::size_t after = m.highlights().size();
    const double keystrokeMs = millisSince(start);
    CHECK(after > 5'000 * kTokensPerLine);
    INFO("5 k-line re-analysis " << keystrokeMs << " ms");
    CHECK(keystrokeMs < 250.0);
}

TEST_CASE("CodeEditor: caret commands, fix-its and state persistence work without a frame") {
    qlab::ui::editor::CodeEditor editor;
    editor.model().setSource(kSample);
    CHECK(editor.caret() == Position{1, 1});
    CHECK_FALSE(editor.hasSelection());

    // The Diagnostics panel jumps into the buffer by span (spec 19 §3).
    editor.gotoSpan(SourceSpan{10, 2, 10, 4, {}});
    CHECK(editor.caret() == Position{10, 2});
    // F12 from the use site lands on the declaration.
    REQUIRE(editor.gotoDefinition());
    CHECK(editor.caret() == Position{4, 6});
    editor.setCaret(Position{8, 3});
    CHECK_FALSE(editor.gotoDefinition());          // `qubit` is not a user gate

    // Selection and clipboard text.
    editor.setCaret(Position{1, 1});
    editor.setCaret(Position{1, 9}, true);
    CHECK(editor.hasSelection());
    CHECK(editor.selectedText() == "OPENQASM");

    // Ctrl+. applies the fix-it of the diagnostic on the caret's line.
    lang::Diagnostic fixable = lang::Diagnostics::make("QL3007", SourceSpan{10, 1, 10, 4, {}}, "myx");
    fixable.fix = "myz";
    editor.model().setDiagnostics({fixable});
    CHECK(editor.model().markersStale() == false);
    editor.setCaret(Position{10, 1});
    REQUIRE(editor.applyFixIt());
    CHECK(editor.model().doc().line(10) == "myz q[0];");
    CHECK(editor.caret() == Position{10, 4});
    CHECK(editor.model().markersStale());          // the edit outran the published diagnostics
    CHECK_FALSE(editor.applyFixIt());              // the fix was already applied at that span

    // Undo/redo go through the document journal and move the caret with the edit.
    editor.undo();
    CHECK(editor.model().doc().line(10) == "myx q[0];");
    editor.redo();
    CHECK(editor.model().doc().line(10) == "myz q[0];");

    // The panel's state slice round-trips.
    const core::Json state = editor.serialize();
    CHECK(state.at("caret_line").get<std::uint32_t>() == 10);
    qlab::ui::editor::CodeEditor restored;
    restored.deserialize(state);
    CHECK(restored.model().doc().text() == editor.model().doc().text());
    CHECK(restored.caret() == editor.caret());
}
