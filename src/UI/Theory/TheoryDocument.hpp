#pragma once
// Spec 20 §5 — Markdown + LaTeX model for the Theory Browser.
// GL-free and ImGui-free: the browser panel walks these blocks and hands the math to
// BasicMathRenderer (spec 20 §1).
#include "Core/Error.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace qlab::ui::theory {

// ---------------------------------------------------------------- links
enum class LinkKind : std::uint8_t {
    None,
    Theory,    // T05-superconducting-qubits.md#6.3-dispersive-readout  → doc "T05", anchor
    Spec,      // spec:14  (optionally spec:14#5)
    Component, // component:attenuator
    Equation,  // eq:transmon_hamiltonian
    Internal,  // #anchor inside the same document
    External,  // http(s):// …
    File,      // any other relative path
};

struct LinkRef {
    LinkKind kind = LinkKind::None;
    std::string raw;    // href exactly as written
    std::string doc;    // "T05" for Theory links
    std::string anchor; // slug after '#'
    std::string id;     // spec number / component id / equation id
};

// Classify a Markdown href (spec 20 §5 link targets).
LinkRef classifyLink(std::string_view href);

// ---------------------------------------------------------------- inline runs
struct InlineSpan {
    std::string text; // literal text, or the LaTeX body when `math` is set
    bool bold = false;
    bool italic = false;
    bool code = false;
    bool math = false; // inline $…$
    LinkRef link;      // kind != None when this run is a link
};

std::vector<InlineSpan> parseInline(std::string_view text);
std::string spansToPlainText(const std::vector<InlineSpan>& spans);

// ---------------------------------------------------------------- blocks
enum class BlockKind : std::uint8_t {
    Heading,
    Paragraph,
    DisplayMath,
    Table,
    Code,
    List,
    Quote,
    Rule,
};

struct TableCell {
    std::vector<InlineSpan> spans;
    std::string plain;
};

enum class ColumnAlign : std::uint8_t { Default, Left, Center, Right };

struct ListItem {
    std::vector<InlineSpan> spans;
    int indent = 0;     // nesting level, 0 = outermost
    std::string marker; // "-", "1." …
};

struct Block {
    BlockKind kind = BlockKind::Paragraph;
    std::uint32_t line = 0; // 1-based source line of the block start

    int level = 0;      // heading level 1–4
    std::string anchor; // heading slug (headings only)
    std::string plain;  // heading/paragraph text without markup; raw body for Code/DisplayMath
    std::vector<InlineSpan> spans; // Heading, Paragraph, Quote

    std::string latex; // DisplayMath body (no $$)
    std::string tag;   // \tag{…} argument if present

    std::string language;                     // fenced code info string
    bool ordered = false;                     // List
    std::vector<ListItem> items;              // List
    std::vector<std::vector<TableCell>> rows; // Table, row 0 is the header
    std::vector<ColumnAlign> align;           // Table
};

struct HeadingRef {
    int level = 0;
    std::string text;
    std::string anchor;
    std::size_t blockIndex = 0;
};

struct TheoryDocument {
    std::string id;    // "T05" (file-name prefix) or the stem when it has no Txx prefix
    std::string title; // first level-1 heading, else the file stem
    std::string path;
    std::vector<Block> blocks;
    std::vector<HeadingRef> headings;

    const HeadingRef* heading(std::string_view anchor) const;
};

// Anchor slug: lowercase; whitespace and dash-like characters collapse to '-';
// a '.' is kept only between two digits ("5.1"); all other punctuation is dropped;
// repeated '-' collapse and leading/trailing '-' are trimmed.
std::string slugify(std::string_view headingText);

// Parse a whole Markdown+LaTeX document. Never fails on content; `id`/`title` come from `path`.
TheoryDocument parseTheoryMarkdown(std::string_view text, std::string_view path = {});

} // namespace qlab::ui::theory
