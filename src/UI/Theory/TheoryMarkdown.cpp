// Block-level Markdown parsing for theory documents (spec 20 §5).
#include "UI/Theory/TheoryDocument.hpp"
#include <cctype>

namespace qlab::ui::theory {
namespace {

std::string_view trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}
bool blank(std::string_view s) { return trim(s).empty(); }
bool startsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t b = 0;
    while (b <= text.size()) {
        std::size_t e = text.find('\n', b);
        if (e == std::string_view::npos) { out.push_back(text.substr(b)); break; }
        out.push_back(text.substr(b, e - b));
        b = e + 1;
    }
    return out;
}

int indentOf(std::string_view s) {
    int n = 0;
    for (char c : s) {
        if (c == ' ') ++n;
        else if (c == '\t') n += 4;
        else break;
    }
    return n;
}

// "- item", "* item", "1. item", "2) item"
bool listMarker(std::string_view line, std::string& marker, bool& ordered, std::size_t& textStart) {
    std::string_view s = line;
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i >= s.size()) return false;
    if ((s[i] == '-' || s[i] == '*' || s[i] == '+') && i + 1 < s.size() && (s[i + 1] == ' ' || s[i + 1] == '\t')) {
        marker = std::string(1, s[i]);
        ordered = false;
        textStart = i + 2;
        return true;
    }
    std::size_t d = i;
    while (d < s.size() && std::isdigit(static_cast<unsigned char>(s[d]))) ++d;
    if (d > i && d < s.size() && (s[d] == '.' || s[d] == ')') && d + 1 < s.size() && s[d + 1] == ' ') {
        marker = std::string(s.substr(i, d - i + 1));
        ordered = true;
        textStart = d + 2;
        return true;
    }
    return false;
}

bool tableDelimiter(std::string_view s, std::vector<ColumnAlign>& align) {
    s = trim(s);
    if (s.find('|') == std::string_view::npos || s.find('-') == std::string_view::npos) return false;
    align.clear();
    std::size_t i = 0;
    if (!s.empty() && s.front() == '|') i = 1;
    std::string cell;
    auto finish = [&] {
        std::string_view c = trim(cell);
        if (c.empty()) return false;
        bool l = c.front() == ':', r = c.back() == ':';
        std::string_view core = c;
        if (l) core.remove_prefix(1);
        if (r && !core.empty()) core.remove_suffix(1);
        if (core.empty()) return false;
        for (char ch : core)
            if (ch != '-') return false;
        align.push_back(l && r ? ColumnAlign::Center : r ? ColumnAlign::Right : l ? ColumnAlign::Left : ColumnAlign::Default);
        return true;
    };
    for (; i < s.size(); ++i) {
        if (s[i] == '|') {
            if (!finish()) return false;
            cell.clear();
        } else cell.push_back(s[i]);
    }
    if (!trim(cell).empty() && !finish()) return false;
    return !align.empty();
}

std::vector<std::string> splitRow(std::string_view s) {
    s = trim(s);
    if (!s.empty() && s.front() == '|') s.remove_prefix(1);
    if (!s.empty() && s.back() == '|') s.remove_suffix(1);
    std::vector<std::string> cells;
    std::string cur;
    bool inCode = false, inMath = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) { cur.push_back(c); cur.push_back(s[++i]); continue; }
        if (c == '`') inCode = !inCode;
        if (c == '$') inMath = !inMath;
        if (c == '|' && !inCode && !inMath) { cells.push_back(std::string(trim(cur))); cur.clear(); continue; }
        cur.push_back(c);
    }
    cells.push_back(std::string(trim(cur)));
    return cells;
}

std::string docIdFromPath(std::string_view path) {
    std::string base(path);
    if (auto sl = base.find_last_of("/\\"); sl != std::string::npos) base = base.substr(sl + 1);
    if (auto dot = base.find_last_of('.'); dot != std::string::npos) base = base.substr(0, dot);
    if (base.size() >= 3 && (base[0] == 'T' || base[0] == 't') && std::isdigit(static_cast<unsigned char>(base[1])) &&
        std::isdigit(static_cast<unsigned char>(base[2])))
        return "T" + base.substr(1, 2);
    return base;
}

// Pull a trailing \tag{...} out of a display-math body.
void extractTag(std::string& latex, std::string& tag) {
    std::size_t p = latex.rfind("\\tag{");
    if (p == std::string::npos) return;
    std::size_t e = latex.find('}', p);
    if (e == std::string::npos) return;
    tag = latex.substr(p + 5, e - p - 5);
    latex.erase(p, e - p + 1);
    while (!latex.empty() && (latex.back() == ' ' || latex.back() == '\n' || latex.back() == '\t')) latex.pop_back();
}

} // namespace

TheoryDocument parseTheoryMarkdown(std::string_view text, std::string_view path) {
    TheoryDocument doc;
    doc.path = std::string(path);
    doc.id = docIdFromPath(path);
    doc.title = doc.id;

    auto lines = splitLines(text);
    const std::size_t n = lines.size();
    std::size_t i = 0;
    bool titleSet = false;

    auto addBlock = [&](Block b) { doc.blocks.push_back(std::move(b)); };

    while (i < n) {
        std::string_view line = lines[i];
        std::uint32_t lineNo = static_cast<std::uint32_t>(i + 1);

        if (blank(line)) { ++i; continue; }

        // Fenced code ``` or ~~~
        std::string_view t = trim(line);
        if (startsWith(t, "```") || startsWith(t, "~~~")) {
            std::string fence(t.substr(0, 3));
            Block b;
            b.kind = BlockKind::Code;
            b.line = lineNo;
            b.language = std::string(trim(t.substr(3)));
            ++i;
            std::string body;
            while (i < n && !startsWith(trim(lines[i]), fence)) {
                body += std::string(lines[i]);
                body += '\n';
                ++i;
            }
            if (i < n) ++i; // closing fence
            b.plain = body;
            addBlock(std::move(b));
            continue;
        }

        // Display math $$ … $$ (opening fence may carry the body on the same line)
        if (startsWith(t, "$$")) {
            Block b;
            b.kind = BlockKind::DisplayMath;
            b.line = lineNo;
            std::string body;
            std::string_view rest = trim(t.substr(2));
            if (rest.size() >= 2 && rest.substr(rest.size() - 2) == "$$") {
                body = std::string(trim(rest.substr(0, rest.size() - 2)));
                ++i;
            } else {
                if (!rest.empty()) { body += std::string(rest); body += '\n'; }
                ++i;
                while (i < n) {
                    std::string_view cur = trim(lines[i]);
                    if (startsWith(cur, "$$")) { ++i; break; }
                    if (cur.size() >= 2 && cur.substr(cur.size() - 2) == "$$") {
                        body += std::string(trim(cur.substr(0, cur.size() - 2)));
                        body += '\n';
                        ++i;
                        break;
                    }
                    body += std::string(lines[i]);
                    body += '\n';
                    ++i;
                }
            }
            while (!body.empty() && (body.back() == '\n' || body.back() == ' ')) body.pop_back();
            b.latex = body;
            extractTag(b.latex, b.tag);
            b.plain = b.latex;
            addBlock(std::move(b));
            continue;
        }

        // Heading
        if (t.front() == '#') {
            std::size_t lvl = 0;
            while (lvl < t.size() && t[lvl] == '#') ++lvl;
            if (lvl <= 6 && lvl < t.size() && (t[lvl] == ' ' || t[lvl] == '\t')) {
                Block b;
                b.kind = BlockKind::Heading;
                b.line = lineNo;
                b.level = static_cast<int>(lvl);
                std::string_view htext = trim(t.substr(lvl));
                b.spans = parseInline(htext);
                b.plain = spansToPlainText(b.spans);
                b.anchor = slugify(htext);
                doc.headings.push_back({b.level, b.plain, b.anchor, doc.blocks.size()});
                if (!titleSet && b.level == 1) { doc.title = b.plain; titleSet = true; }
                addBlock(std::move(b));
                ++i;
                continue;
            }
        }

        // Horizontal rule
        if (t == "---" || t == "***" || t == "___" || t == "- - -") {
            Block b;
            b.kind = BlockKind::Rule;
            b.line = lineNo;
            addBlock(std::move(b));
            ++i;
            continue;
        }

        // Table: header row followed by a delimiter row
        std::vector<ColumnAlign> align;
        if (t.find('|') != std::string_view::npos && i + 1 < n && tableDelimiter(lines[i + 1], align)) {
            Block b;
            b.kind = BlockKind::Table;
            b.line = lineNo;
            b.align = align;
            auto makeRow = [](const std::vector<std::string>& cells) {
                std::vector<TableCell> row;
                for (const auto& c : cells) {
                    TableCell tc;
                    tc.spans = parseInline(c);
                    tc.plain = spansToPlainText(tc.spans);
                    row.push_back(std::move(tc));
                }
                return row;
            };
            b.rows.push_back(makeRow(splitRow(lines[i])));
            i += 2;
            while (i < n && !blank(lines[i]) && trim(lines[i]).find('|') != std::string_view::npos) {
                b.rows.push_back(makeRow(splitRow(lines[i])));
                ++i;
            }
            addBlock(std::move(b));
            continue;
        }

        // Block quote
        if (t.front() == '>') {
            Block b;
            b.kind = BlockKind::Quote;
            b.line = lineNo;
            std::string body;
            while (i < n && !blank(lines[i]) && trim(lines[i]).front() == '>') {
                std::string_view cur = trim(lines[i]);
                cur.remove_prefix(1);
                if (!body.empty()) body += ' ';
                body += std::string(trim(cur));
                ++i;
            }
            b.spans = parseInline(body);
            b.plain = spansToPlainText(b.spans);
            addBlock(std::move(b));
            continue;
        }

        // List
        {
            std::string marker;
            bool ordered = false;
            std::size_t ts = 0;
            if (listMarker(line, marker, ordered, ts)) {
                Block b;
                b.kind = BlockKind::List;
                b.line = lineNo;
                b.ordered = ordered;
                while (i < n) {
                    std::string m2;
                    bool o2 = false;
                    std::size_t ts2 = 0;
                    if (!listMarker(lines[i], m2, o2, ts2)) {
                        // Continuation line of the previous item (indented, non-blank).
                        if (!b.items.empty() && !blank(lines[i]) && indentOf(lines[i]) > 0) {
                            std::string extra(trim(lines[i]));
                            auto more = parseInline(" " + extra);
                            auto& sp = b.items.back().spans;
                            sp.insert(sp.end(), more.begin(), more.end());
                            b.items.back().spans = sp;
                            ++i;
                            continue;
                        }
                        break;
                    }
                    ListItem it;
                    it.marker = m2;
                    it.indent = indentOf(lines[i]) / 2;
                    it.spans = parseInline(trim(lines[i].substr(ts2)));
                    b.items.push_back(std::move(it));
                    ++i;
                }
                addBlock(std::move(b));
                continue;
            }
        }

        // Paragraph: consume until a blank line or the start of another block.
        {
            Block b;
            b.kind = BlockKind::Paragraph;
            b.line = lineNo;
            std::string body;
            while (i < n && !blank(lines[i])) {
                std::string_view cur = trim(lines[i]);
                if (cur.front() == '#' || startsWith(cur, "```") || startsWith(cur, "~~~") || startsWith(cur, "$$") ||
                    cur.front() == '>' || cur == "---")
                    break;
                std::string m2;
                bool o2 = false;
                std::size_t ts2 = 0;
                if (listMarker(lines[i], m2, o2, ts2)) break;
                std::vector<ColumnAlign> a2;
                if (cur.find('|') != std::string_view::npos && i + 1 < n && tableDelimiter(lines[i + 1], a2)) break;
                if (!body.empty()) body += ' ';
                body += std::string(cur);
                ++i;
            }
            if (body.empty()) { ++i; continue; }
            b.spans = parseInline(body);
            b.plain = spansToPlainText(b.spans);
            addBlock(std::move(b));
        }
    }
    return doc;
}

} // namespace qlab::ui::theory
