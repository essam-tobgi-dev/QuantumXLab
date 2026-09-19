// Spec 19 §3 — editor analysis: highlighting, markers, definitions (see EditorModel.hpp).
#include "UI/Editor/EditorModel.hpp"
#include "Lang/Lexer.hpp"
#include "Lang/StdGates.hpp"
#include <algorithm>
#include <array>
#include <cctype>

namespace qlab::ui::editor {
namespace {

// Spec 13 §7 `pragma qlab.*` keys, as `lang::Pragmas` names them.
constexpr std::array<std::string_view, 17> kPragmaKeys{
    "qlab.device",     "qlab.backend",        "qlab.shots",    "qlab.seed",      "qlab.noise",
    "qlab.layout",     "qlab.routing",        "qlab.optimize", "qlab.sweep",     "qlab.probe",
    "qlab.rb",         "qlab.pulse_level",    "qlab.snapshot", "qlab.alignment", "qlab.assert",
    "qlab.noise_file", "qlab.compare_devices"};

constexpr std::string_view kOpenBrackets = "([{";
constexpr std::string_view kCloseBrackets = ")]}";

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

} // namespace

std::span<const std::string_view> pragmaKeys() {
    return kPragmaKeys;
}

std::string_view completionKindName(CompletionKind k) {
    switch (k) {
    case CompletionKind::Keyword:
        return "keyword";
    case CompletionKind::Type:
        return "type";
    case CompletionKind::Gate:
        return "gate";
    case CompletionKind::Builtin:
        return "builtin";
    case CompletionKind::UserGate:
        return "gate (user)";
    case CompletionKind::Defcal:
        return "defcal";
    case CompletionKind::Qubit:
        return "qubit";
    case CompletionKind::Register:
        return "register";
    case CompletionKind::Pragma:
        return "pragma";
    }
    return "?";
}

void EditorModel::setDiagnostics(std::vector<lang::Diagnostic> diagnostics) {
    diagnostics_ = std::move(diagnostics);
    errors_ = warnings_ = 0;
    markers_.clear();
    for (const lang::Diagnostic& d : diagnostics_) {
        if (d.severity == lang::Severity::Error)
            ++errors_;
        else if (d.severity == lang::Severity::Warning)
            ++warnings_;
        Marker m;
        m.severity = d.severity;
        m.id = d.id();
        m.message = d.error.message;
        m.fix = d.fix;
        if (d.error.span) {
            const SourceSpan& s = *d.error.span;
            m.line = std::max<std::uint32_t>(1, s.line);
            m.begin = std::max<std::uint32_t>(1, s.column);
            // A span ending on a later line squiggles to the end of its first line.
            m.end = s.endLine == s.line && s.endColumn > s.column ? s.endColumn
                                                                  : doc_.lineLength(m.line) + 1;
        } else {
            m.end = doc_.lineLength(1) + 1;
        }
        markers_.push_back(std::move(m));
    }
    std::stable_sort(markers_.begin(), markers_.end(), [](const Marker& a, const Marker& b) {
        return std::tie(a.line, a.begin) < std::tie(b.line, b.begin);
    });
    markerRevision_ = doc_.revision();
}

void EditorModel::analyse() const {
    if (analysedRevision_ == doc_.revision() && !highlights_.empty())
        return;
    analysedRevision_ = doc_.revision();
    // Lenient: never fails, keeps comments and Invalid tokens (spec 13 §11).
    tokens_ = lang::tokenizeLenient(doc_.text());
    highlights_.clear();
    definitions_.clear();
    declared_.clear();
    highlights_.reserve(tokens_.size());

    // Pass 1 — declarations, so that a user gate is coloured as a gate wherever it is USED, not
    // only where it is declared (spec 19 §3 token-kind highlighting over the 13 §2 kinds).
    for (std::size_t i = 0; i < tokens_.size(); ++i) {
        const lang::Token& t = tokens_[i];
        // `gate name(...) q { … }` and `defcal name ... q { … }` declare jump targets (spec 19 §3).
        const bool isGate = t.is(lang::TokenKind::Keyword, "gate");
        const bool isDefcal = t.isText("defcal");
        if ((isGate || isDefcal) && i + 1 < tokens_.size()) {
            const lang::Token& name = tokens_[i + 1];
            if (name.kind == lang::TokenKind::Identifier || name.kind == lang::TokenKind::Gate) {
                Definition d;
                d.name = name.text;
                d.at = Position{std::max<std::uint32_t>(1, name.span.line),
                                std::max<std::uint32_t>(1, name.span.column)};
                d.defcal = isDefcal;
                d.signature = std::string(isDefcal ? "defcal " : "gate ") + name.text;
                definitions_.push_back(d);
                declared_.push_back(
                    Completion{d.name, isDefcal ? CompletionKind::Defcal : CompletionKind::UserGate,
                               d.signature});
            }
        }
        // `qubit[n] q;` / `bit[n] c;` / `qubit q;` declare completion candidates.
        if (t.kind == lang::TokenKind::Type &&
            (t.text == "qubit" || t.text == "bit" || t.text == "int" || t.text == "float" ||
             t.text == "angle" || t.text == "complex")) {
            std::size_t j = i + 1;
            if (j < tokens_.size() && tokens_[j].isText("[")) { // skip the [size] part
                int depth = 0;
                while (j < tokens_.size()) {
                    if (tokens_[j].isText("["))
                        ++depth;
                    if (tokens_[j].isText("]") && --depth == 0) {
                        ++j;
                        break;
                    }
                    ++j;
                }
            }
            if (j < tokens_.size() && tokens_[j].kind == lang::TokenKind::Identifier)
                declared_.push_back(Completion{
                    tokens_[j].text,
                    t.text == "qubit" ? CompletionKind::Qubit : CompletionKind::Register, t.text});
        }
    }

    // Pass 2 — the highlight runs. `end` is exclusive; a token that spans lines (block comment,
    // pragma) highlights to the end of its first line.
    for (const lang::Token& t : tokens_) {
        if (t.kind == lang::TokenKind::Eof)
            continue;
        HighlightSpan h;
        h.line = std::max<std::uint32_t>(1, t.span.line);
        h.begin = std::max<std::uint32_t>(1, t.span.column);
        h.end = t.span.endLine == t.span.line ? std::max(h.begin + 1, t.span.endColumn)
                                              : doc_.lineLength(h.line) + 1;
        h.kind = t.kind;
        // The lexer marks only `U`, `gphase` and `CX` as Gate (`classifyWord`); an `stdgates.inc`
        // name or a user gate is an Identifier there, and the editor colours it as the gate it is.
        if (h.kind == lang::TokenKind::Identifier) {
            if (lang::StdGates::isStd(t.text))
                h.kind = lang::TokenKind::Gate;
            else
                for (const Definition& d : definitions_)
                    if (d.name == t.text && !d.defcal) {
                        h.kind = lang::TokenKind::Gate;
                        break;
                    }
        }
        highlights_.push_back(h);
    }

    // Per-line index so drawing a visible range is O(visible lines).
    lineIndex_.assign(static_cast<std::size_t>(doc_.lineCount()) + 2, highlights_.size());
    for (std::size_t i = highlights_.size(); i-- > 0;) {
        const std::size_t l = highlights_[i].line;
        if (l < lineIndex_.size())
            lineIndex_[l] = i;
    }
    for (std::size_t l = lineIndex_.size(); l-- > 1;)
        if (lineIndex_[l - 1] > lineIndex_[l])
            lineIndex_[l - 1] = lineIndex_[l];
}

std::span<const HighlightSpan> EditorModel::highlights() const {
    analyse();
    return highlights_;
}

std::span<const HighlightSpan> EditorModel::highlightsOn(std::uint32_t line) const {
    analyse();
    if (line == 0 || line + 1 >= lineIndex_.size())
        return {};
    const std::size_t begin = lineIndex_[line];
    std::size_t end = begin;
    while (end < highlights_.size() && highlights_[end].line == line)
        ++end;
    return std::span<const HighlightSpan>(highlights_).subspan(begin, end - begin);
}

std::span<const Marker> EditorModel::markers() const {
    return markers_;
}

std::vector<const Marker*> EditorModel::markersOn(std::uint32_t line) const {
    std::vector<const Marker*> out;
    for (const Marker& m : markers_)
        if (m.line == line)
            out.push_back(&m);
    return out;
}

std::span<const Definition> EditorModel::definitions() const {
    analyse();
    return definitions_;
}

std::optional<Definition> EditorModel::definitionOf(std::string_view name) const {
    analyse();
    for (const Definition& d : definitions_)
        if (d.name == name)
            return d;
    return std::nullopt;
}

std::string EditorModel::wordAt(Position p) const {
    const Position c = doc_.clamp(p);
    const std::string_view text = doc_.line(c.line);
    const std::size_t at = doc_.columnToByte(c.line, c.column);
    if (text.empty())
        return {};
    std::size_t begin = std::min(at, text.size());
    while (begin > 0 && isIdentChar(text[begin - 1]))
        --begin;
    std::size_t end = std::min(at, text.size());
    while (end < text.size() && isIdentChar(text[end]))
        ++end;
    return std::string(text.substr(begin, end - begin));
}

const GateDocEntry* EditorModel::hoverDoc(Position p, const TheoryAssets& assets) const {
    const std::string word = wordAt(p);
    return word.empty() ? nullptr : assets.gate(word);
}

std::optional<Position> EditorModel::matchingBracket(Position p) const {
    const Position c = doc_.clamp(p);
    const std::string& text = doc_.text();
    std::size_t at = doc_.offsetOf(c);
    // The bracket under the caret, else the one just before it (the usual editor convention).
    char here = at < text.size() ? text[at] : '\0';
    if (kOpenBrackets.find(here) == std::string_view::npos &&
        kCloseBrackets.find(here) == std::string_view::npos) {
        if (at == 0)
            return std::nullopt;
        --at;
        here = text[at];
    }
    const std::size_t open = kOpenBrackets.find(here);
    const std::size_t close = kCloseBrackets.find(here);
    if (open == std::string_view::npos && close == std::string_view::npos)
        return std::nullopt;
    const bool forward = open != std::string_view::npos;
    const char want = forward ? kCloseBrackets[open] : kOpenBrackets[close];
    int depth = 0;
    if (forward) {
        for (std::size_t i = at; i < text.size(); ++i) {
            if (text[i] == here)
                ++depth;
            else if (text[i] == want && --depth == 0)
                return doc_.positionOf(i);
        }
    } else {
        for (std::size_t i = at + 1; i-- > 0;) {
            if (text[i] == here)
                ++depth;
            else if (text[i] == want && --depth == 0)
                return doc_.positionOf(i);
            if (i == 0)
                break;
        }
    }
    return std::nullopt;
}

std::string EditorModel::indentAfter(std::uint32_t line) const {
    const std::string_view s = doc_.line(line);
    std::size_t spaces = 0;
    while (spaces < s.size() && (s[spaces] == ' ' || s[spaces] == '\t'))
        ++spaces;
    std::string indent(s.substr(0, spaces));
    // One extra level after a line that leaves a brace open.
    int depth = 0;
    for (char c : s) {
        if (c == '{')
            ++depth;
        else if (c == '}')
            --depth;
    }
    if (depth > 0)
        indent += "  ";
    return indent;
}

std::vector<Completion> EditorModel::completions(Position p) const {
    analyse();
    const Position c = doc_.clamp(p);
    const std::string_view text = doc_.line(c.line);
    const std::size_t at = doc_.columnToByte(c.line, c.column);
    std::size_t begin = std::min(at, text.size());
    while (begin > 0 && isIdentChar(text[begin - 1]))
        --begin;
    const std::string_view prefix = text.substr(begin, std::min(at, text.size()) - begin);

    std::vector<Completion> out;
    const auto add = [&](std::string name, CompletionKind kind, std::string detail) {
        if (!prefix.empty() && !std::string_view(name).starts_with(prefix))
            return;
        if (std::find_if(out.begin(), out.end(),
                         [&](const Completion& e) { return e.text == name; }) != out.end())
            return;
        out.push_back(Completion{std::move(name), kind, std::move(detail)});
    };

    // Spec 19 §3: a `pragma` line completes its keys and nothing else.
    const std::string_view head = text.substr(0, begin);
    if (head.find("pragma") != std::string_view::npos) {
        for (std::string_view key : kPragmaKeys)
            add(std::string(key), CompletionKind::Pragma, "pragma");
        return out;
    }
    for (const Completion& d : declared_)
        add(d.text, d.kind, d.detail);
    for (const lang::GateInfo& g : lang::StdGates::all())
        add(std::string(g.name), CompletionKind::Gate, std::string(g.signature));
    for (std::string_view kw :
         {"OPENQASM", "include", "gate",   "def",     "defcal", "cal",     "if",  "else",
          "for",      "while",   "return", "measure", "reset",  "barrier", "box", "delay",
          "input",    "output",  "const",  "let",     "extern", "pragma"})
        add(std::string(kw), CompletionKind::Keyword, "keyword");
    for (std::string_view ty : {"int", "uint", "float", "angle", "bool", "bit", "duration",
                                "stretch", "complex", "qubit", "array"})
        add(std::string(ty), CompletionKind::Type, "type");
    for (std::string_view b : {"pi", "euler", "tau", "sin", "cos", "tan", "exp", "ln", "sqrt",
                               "popcount", "rotl", "rotr", "durationof", "sizeof"})
        add(std::string(b), CompletionKind::Builtin, "builtin");

    // Declared names first, then the standard library, then the language (the `add` order above is
    // already that, so only a stable sort by kind rank is needed).
    std::stable_sort(out.begin(), out.end(), [](const Completion& a, const Completion& b) {
        const auto rank = [](CompletionKind k) {
            switch (k) {
            case CompletionKind::Qubit:
            case CompletionKind::Register:
                return 0;
            case CompletionKind::UserGate:
            case CompletionKind::Defcal:
                return 1;
            case CompletionKind::Gate:
                return 2;
            case CompletionKind::Pragma:
                return 3;
            case CompletionKind::Builtin:
                return 4;
            case CompletionKind::Type:
                return 5;
            case CompletionKind::Keyword:
                return 6;
            }
            return 7;
        };
        return rank(a.kind) < rank(b.kind);
    });
    return out;
}

Position EditorModel::applyCompletion(Position p, const Completion& c) {
    const Position at = doc_.clamp(p);
    const std::string_view text = doc_.line(at.line);
    const std::size_t byte = doc_.columnToByte(at.line, at.column);
    std::size_t begin = std::min(byte, text.size());
    while (begin > 0 && isIdentChar(text[begin - 1]))
        --begin;
    const Position from{at.line, doc_.byteToColumn(at.line, begin)};
    doc_.replace(from, at, c.text);
    return Position{at.line, from.column + codePoints(c.text)};
}

} // namespace qlab::ui::editor
