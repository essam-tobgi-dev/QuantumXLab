// Inline Markdown runs, link classification, and the anchor slug rule (spec 20 §5).
#include "UI/Theory/TheoryDocument.hpp"
#include <cctype>

namespace qlab::ui::theory {
namespace {

bool startsWith(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

// UTF-8 dash-like characters that behave as word separators in headings.
// U+2010 ‐, U+2011 ‑, U+2012 ‒, U+2013 –, U+2014 —, U+2015 ―, U+2212 −
bool dashAt(std::string_view s, std::size_t i, std::size_t& len) {
    auto b = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    if (i + 2 < s.size() && b(i) == 0xE2 && b(i + 1) == 0x80 && b(i + 2) >= 0x90 &&
        b(i + 2) <= 0x95) {
        len = 3;
        return true;
    }
    if (i + 2 < s.size() && b(i) == 0xE2 && b(i + 1) == 0x88 && b(i + 2) == 0x92) {
        len = 3;
        return true;
    }
    return false;
}

void pushRun(std::vector<InlineSpan>& out, std::string& buf, bool bold, bool italic) {
    if (buf.empty())
        return;
    InlineSpan s;
    s.text = buf;
    s.bold = bold;
    s.italic = italic;
    out.push_back(std::move(s));
    buf.clear();
}

} // namespace

std::string slugify(std::string_view h) {
    // Strip a leading "#"-run and surrounding blanks first (callers usually pass clean text).
    std::size_t b = 0;
    while (b < h.size() && (h[b] == '#' || h[b] == ' ' || h[b] == '\t'))
        ++b;
    std::size_t e = h.size();
    while (e > b && (h[e - 1] == ' ' || h[e - 1] == '\t'))
        --e;
    h = h.substr(b, e - b);

    std::string out;
    out.reserve(h.size());
    for (std::size_t i = 0; i < h.size();) {
        unsigned char c = static_cast<unsigned char>(h[i]);
        std::size_t dlen = 0;
        if (dashAt(h, i, dlen)) {
            out.push_back('-');
            i += dlen;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '-' || c == '_' || c == '/') {
            out.push_back('-');
            ++i;
            continue;
        }
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
            ++i;
            continue;
        }
        if (c == '.') {
            // Keep a dot only inside a number: digit '.' digit.
            bool prevDigit = !out.empty() && std::isdigit(static_cast<unsigned char>(out.back()));
            bool nextDigit = i + 1 < h.size() && std::isdigit(static_cast<unsigned char>(h[i + 1]));
            if (prevDigit && nextDigit)
                out.push_back('.');
            ++i;
            continue;
        }
        if (c >= 0x80) {
            // Any other non-ASCII (Greek, accented letters) is dropped; it never appears in
            // the shipped anchors and dropping keeps slugs ASCII-stable.
            std::size_t n = 1;
            if ((c & 0xE0) == 0xC0)
                n = 2;
            else if ((c & 0xF0) == 0xE0)
                n = 3;
            else if ((c & 0xF8) == 0xF0)
                n = 4;
            i += n;
            continue;
        }
        ++i; // drop every other punctuation character
    }
    // Collapse repeated hyphens, trim.
    std::string s;
    s.reserve(out.size());
    for (char c : out) {
        if (c == '-' && (s.empty() || s.back() == '-'))
            continue;
        s.push_back(c);
    }
    while (!s.empty() && s.back() == '-')
        s.pop_back();
    return s;
}

LinkRef classifyLink(std::string_view href) {
    LinkRef r;
    r.raw = std::string(href);
    if (href.empty())
        return r;
    if (startsWith(href, "http://") || startsWith(href, "https://") ||
        startsWith(href, "mailto:")) {
        r.kind = LinkKind::External;
        return r;
    }
    if (startsWith(href, "spec:")) {
        r.kind = LinkKind::Spec;
        std::string rest(href.substr(5));
        if (auto h = rest.find('#'); h != std::string::npos) {
            r.anchor = rest.substr(h + 1);
            rest = rest.substr(0, h);
        }
        r.id = rest;
        return r;
    }
    if (startsWith(href, "component:")) {
        r.kind = LinkKind::Component;
        r.id = std::string(href.substr(10));
        return r;
    }
    if (startsWith(href, "eq:")) {
        r.kind = LinkKind::Equation;
        r.id = std::string(href.substr(3));
        return r;
    }
    if (href[0] == '#') {
        r.kind = LinkKind::Internal;
        r.anchor = std::string(href.substr(1));
        return r;
    }

    std::string path(href), anchor;
    if (auto h = path.find('#'); h != std::string::npos) {
        anchor = path.substr(h + 1);
        path = path.substr(0, h);
    }
    // "T05-superconducting-qubits.md" / "T05.md" / "T05" → theory document T05.
    std::string base = path;
    if (auto sl = base.find_last_of('/'); sl != std::string::npos)
        base = base.substr(sl + 1);
    if (base.size() >= 3 && (base[0] == 'T' || base[0] == 't') &&
        std::isdigit(static_cast<unsigned char>(base[1])) &&
        std::isdigit(static_cast<unsigned char>(base[2]))) {
        r.kind = LinkKind::Theory;
        r.doc = "T" + base.substr(1, 2);
        r.anchor = anchor;
        return r;
    }
    r.kind = LinkKind::File;
    r.anchor = anchor;
    return r;
}

std::vector<InlineSpan> parseInline(std::string_view t) {
    std::vector<InlineSpan> out;
    std::string buf;
    bool bold = false, italic = false;
    auto flush = [&] { pushRun(out, buf, bold, italic); };

    for (std::size_t i = 0; i < t.size();) {
        char c = t[i];
        if (c == '\\' && i + 1 < t.size() && !std::isalnum(static_cast<unsigned char>(t[i + 1]))) {
            // Escaped punctuation, e.g. \$ or \*.
            buf.push_back(t[i + 1]);
            i += 2;
            continue;
        }
        if (c == '`') { // inline code
            std::size_t e = t.find('`', i + 1);
            if (e == std::string_view::npos) {
                buf.push_back(c);
                ++i;
                continue;
            }
            flush();
            InlineSpan s;
            s.text = std::string(t.substr(i + 1, e - i - 1));
            s.code = true;
            out.push_back(std::move(s));
            i = e + 1;
            continue;
        }
        if (c == '$') { // inline math (a lone '$' stays literal)
            std::size_t e = i + 1;
            while (e < t.size() && !(t[e] == '$' && t[e - 1] != '\\'))
                ++e;
            if (e >= t.size()) {
                buf.push_back(c);
                ++i;
                continue;
            }
            flush();
            InlineSpan s;
            s.text = std::string(t.substr(i + 1, e - i - 1));
            s.math = true;
            out.push_back(std::move(s));
            i = e + 1;
            continue;
        }
        if (c == '*' || c == '_') {
            bool dbl = i + 1 < t.size() && t[i + 1] == c;
            // '_' inside a word (snake_case) is literal.
            bool wordInner = c == '_' && !buf.empty() &&
                             std::isalnum(static_cast<unsigned char>(buf.back())) &&
                             i + 1 < t.size() && std::isalnum(static_cast<unsigned char>(t[i + 1]));
            if (!wordInner) {
                flush();
                if (dbl) {
                    bold = !bold;
                    i += 2;
                } else {
                    italic = !italic;
                    ++i;
                }
                continue;
            }
        }
        if (c == '[') { // [text](href)
            std::size_t close = t.find(']', i + 1);
            if (close != std::string_view::npos && close + 1 < t.size() && t[close + 1] == '(') {
                std::size_t end = t.find(')', close + 2);
                if (end != std::string_view::npos) {
                    flush();
                    InlineSpan s;
                    s.text = std::string(t.substr(i + 1, close - i - 1));
                    s.bold = bold;
                    s.italic = italic;
                    s.link = classifyLink(t.substr(close + 2, end - close - 2));
                    out.push_back(std::move(s));
                    i = end + 1;
                    continue;
                }
            }
        }
        buf.push_back(c);
        ++i;
    }
    flush();
    return out;
}

std::string spansToPlainText(const std::vector<InlineSpan>& spans) {
    std::string s;
    for (const auto& sp : spans) {
        if (sp.math) {
            s += '$';
            s += sp.text;
            s += '$';
        } else
            s += sp.text;
    }
    return s;
}

const HeadingRef* TheoryDocument::heading(std::string_view anchor) const {
    for (const auto& h : headings)
        if (h.anchor == anchor)
            return &h;
    return nullptr;
}

} // namespace qlab::ui::theory
