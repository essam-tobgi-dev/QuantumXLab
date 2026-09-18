#include "Report/Html.hpp"
#include "Data/Fidelity.hpp"

#include "Data/Writers.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <set>

namespace qlab::report {
namespace {

constexpr std::string_view kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

const std::set<std::string, std::less<>>& qasmKeywords() {
    static const std::set<std::string, std::less<>> kw{
        "OPENQASM", "include", "pragma",  "qubit",  "qreg",   "bit",     "creg",   "int",    "uint",
        "float",    "angle",   "bool",    "complex", "duration", "stretch", "const",  "input",  "output",
        "gate",     "def",     "defcal",  "cal",    "extern", "let",     "if",     "else",   "for",
        "while",    "in",      "return",  "break",  "continue", "end",   "measure", "reset",  "barrier",
        "delay",    "box",     "ctrl",    "negctrl", "inv",   "pow",     "array",  "sizeof", "true",
        "false",    "switch",  "case",    "default"};
    return kw;
}

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Inline SVG inside HTML needs no `xmlns`: the HTML parser puts `<svg>` in the SVG namespace, and
// leaving the attribute out keeps the document free of any URL at all (spec 23 §10).
std::string svgHead(int width, int height) {
    return std::format(R"(<svg class="fig" viewBox="0 0 {} {}" width="100%" role="img">)", width, height);
}

std::string fmt(double v, int digits = 4) {
    std::string s = std::format("{:.{}g}", v, digits);
    return s;
}

} // namespace

std::string htmlEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out += c;
        }
    }
    return out;
}

std::string base64(std::span<const std::uint8_t> bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const std::uint32_t v = (std::uint32_t(bytes[i]) << 16) | (std::uint32_t(bytes[i + 1]) << 8) | bytes[i + 2];
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
        out += kB64[v & 63];
    }
    if (i + 1 == bytes.size()) {
        const std::uint32_t v = std::uint32_t(bytes[i]) << 16;
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const std::uint32_t v = (std::uint32_t(bytes[i]) << 16) | (std::uint32_t(bytes[i + 1]) << 8);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

std::string dataUri(std::string_view mime, std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return {};
    return "data:" + std::string(mime) + ";base64," + base64(bytes);
}

std::string substitute(std::string_view tmpl, const Placeholders& values, std::vector<std::string>* missing) {
    std::string out;
    out.reserve(tmpl.size() + 4096);
    std::size_t pos = 0;
    while (pos < tmpl.size()) {
        const std::size_t open = tmpl.find("{{", pos);
        if (open == std::string_view::npos) {
            out += tmpl.substr(pos);
            break;
        }
        out += tmpl.substr(pos, open - pos);
        const std::size_t close = tmpl.find("}}", open + 2);
        if (close == std::string_view::npos) {   // unterminated: copy through, never truncate
            out += tmpl.substr(open);
            break;
        }
        std::string_view name = tmpl.substr(open + 2, close - open - 2);
        while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
        while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
        if (auto it = values.find(name); it != values.end()) {
            out += it->second;
        } else if (missing) {
            missing->emplace_back(name);
        }
        pos = close + 2;
    }
    return out;
}

std::string highlightQasm(std::string_view source) {
    std::string out;
    out.reserve(source.size() * 2);
    std::size_t i = 0;
    const std::size_t n = source.size();
    while (i < n) {
        const char c = source[i];
        if (c == '/' && i + 1 < n && source[i + 1] == '/') {
            const std::size_t eol = source.find('\n', i);
            const std::size_t end = eol == std::string_view::npos ? n : eol;
            out += "<span class=\"c\">" + htmlEscape(source.substr(i, end - i)) + "</span>";
            i = end;
        } else if (c == '/' && i + 1 < n && source[i + 1] == '*') {
            const std::size_t close = source.find("*/", i + 2);
            const std::size_t end = close == std::string_view::npos ? n : close + 2;
            out += "<span class=\"c\">" + htmlEscape(source.substr(i, end - i)) + "</span>";
            i = end;
        } else if (c == '"') {
            std::size_t end = i + 1;
            while (end < n && source[end] != '"') ++end;
            if (end < n) ++end;
            out += "<span class=\"s\">" + htmlEscape(source.substr(i, end - i)) + "</span>";
            i = end;
        } else if (c == '$') {
            std::size_t end = i + 1;
            while (end < n && std::isdigit(static_cast<unsigned char>(source[end]))) ++end;
            out += "<span class=\"g\">" + htmlEscape(source.substr(i, end - i)) + "</span>";
            i = end;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t end = i;
            while (end < n && (std::isalnum(static_cast<unsigned char>(source[end])) || source[end] == '.')) ++end;
            out += "<span class=\"n\">" + htmlEscape(source.substr(i, end - i)) + "</span>";
            i = end;
        } else if (identStart(c)) {
            std::size_t end = i;
            while (end < n && identChar(source[end])) ++end;
            const std::string_view word = source.substr(i, end - i);
            if (qasmKeywords().contains(word))
                out += "<span class=\"k\">" + htmlEscape(word) + "</span>";
            else
                out += htmlEscape(word);
            i = end;
        } else {
            out += htmlEscape(source.substr(i, 1));
            ++i;
        }
    }
    return out;
}

std::string tableHtml(std::span<const std::string> headers, std::span<const std::vector<std::string>> rows,
                      std::string_view cssClass) {
    std::string out = "<table class=\"" + std::string(cssClass) + "\"><thead><tr>";
    for (const auto& h : headers) out += "<th>" + htmlEscape(h) + "</th>";
    out += "</tr></thead><tbody>";
    for (const auto& row : rows) {
        out += "<tr>";
        for (const auto& cell : row) out += "<td>" + htmlEscape(cell) + "</td>";
        out += "</tr>";
    }
    out += "</tbody></table>";
    return out;
}

std::string fidelityBadge(data::FidelityClass cls, std::string_view prefix) {
    std::string name(data::fidelityName(cls));
    std::string lower;
    for (char c : name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return "<span class=\"badge " + lower + "\">" + htmlEscape(prefix) + (prefix.empty() ? "" : " ") + name + "</span>";
}

std::string histogramSvg(const data::Histogram& h, std::size_t topK, int width, int height) {
    const auto top = h.topK(topK);
    if (top.empty()) return "<p class=\"muted\">no counts</p>";
    const double total = h.total() ? static_cast<double>(h.total()) : 1.0;
    const int left = 48, right = 12, top_ = 12, bottom = 56;
    const int plotW = width - left - right, plotH = height - top_ - bottom;
    double maxP = 0.0;
    for (const auto& [label, count] : top) maxP = std::max(maxP, static_cast<double>(count) / total);
    if (maxP <= 0.0) maxP = 1.0;

    std::string svg = svgHead(width, height);
    svg += std::format(R"(<line class="ax" x1="{}" y1="{}" x2="{}" y2="{}"/>)", left, top_ + plotH, left + plotW,
                       top_ + plotH);
    svg += std::format(R"(<line class="ax" x1="{}" y1="{}" x2="{}" y2="{}"/>)", left, top_, left, top_ + plotH);
    const double slot = static_cast<double>(plotW) / static_cast<double>(top.size());
    const double barW = std::max(2.0, slot * 0.7);
    for (std::size_t i = 0; i < top.size(); ++i) {
        const double p = static_cast<double>(top[i].second) / total;
        const double bh = plotH * p / maxP;
        const double x = left + slot * static_cast<double>(i) + (slot - barW) / 2.0;
        const double y = top_ + plotH - bh;
        svg += std::format(R"(<rect class="bar" x="{:.2f}" y="{:.2f}" width="{:.2f}" height="{:.2f}"><title>{} : {} ({:.4g})</title></rect>)",
                           x, y, barW, bh, htmlEscape(top[i].first), top[i].second, p);
        svg += std::format(R"SVG(<text class="tick" x="{:.2f}" y="{}" transform="rotate(-60 {:.2f} {})">{}</text>)SVG",
                           x + barW / 2.0, top_ + plotH + 14, x + barW / 2.0, top_ + plotH + 14,
                           htmlEscape(top[i].first));
    }
    svg += std::format(R"(<text class="tick" x="{}" y="{}">{}</text>)", 4, top_ + 8, fmt(maxP));
    svg += std::format(R"(<text class="tick" x="{}" y="{}">0</text>)", 30, top_ + plotH);
    svg += "</svg>";
    return svg;
}

std::string seriesSvg(std::span<const double> x, std::span<const double> y, std::span<const double> sigma,
                      std::string_view xLabel, std::string_view yLabel, int width, int height) {
    if (x.empty() || y.size() != x.size()) return "<p class=\"muted\">no samples</p>";
    const int left = 56, right = 12, top_ = 12, bottom = 40;
    const int plotW = width - left - right, plotH = height - top_ - bottom;
    const auto [xmin, xmax] = std::minmax_element(x.begin(), x.end());
    double ymin = *std::min_element(y.begin(), y.end());
    double ymax = *std::max_element(y.begin(), y.end());
    if (!sigma.empty() && sigma.size() == y.size())
        for (std::size_t i = 0; i < y.size(); ++i) {
            ymin = std::min(ymin, y[i] - sigma[i]);
            ymax = std::max(ymax, y[i] + sigma[i]);
        }
    const double dx = (*xmax - *xmin) != 0.0 ? (*xmax - *xmin) : 1.0;
    const double dy = (ymax - ymin) != 0.0 ? (ymax - ymin) : 1.0;
    auto px = [&](double v) { return left + plotW * (v - *xmin) / dx; };
    auto py = [&](double v) { return top_ + plotH - plotH * (v - ymin) / dy; };

    std::string svg = svgHead(width, height);
    svg += std::format(R"(<line class="ax" x1="{}" y1="{}" x2="{}" y2="{}"/>)", left, top_ + plotH, left + plotW,
                       top_ + plotH);
    svg += std::format(R"(<line class="ax" x1="{}" y1="{}" x2="{}" y2="{}"/>)", left, top_, left, top_ + plotH);
    if (!sigma.empty() && sigma.size() == y.size()) {
        std::string band = "<polygon class=\"band\" points=\"";
        for (std::size_t i = 0; i < x.size(); ++i) band += std::format("{:.2f},{:.2f} ", px(x[i]), py(y[i] + sigma[i]));
        for (std::size_t i = x.size(); i-- > 0;) band += std::format("{:.2f},{:.2f} ", px(x[i]), py(y[i] - sigma[i]));
        band += "\"/>";
        svg += band;
    }
    std::string path = "<polyline class=\"line\" points=\"";
    for (std::size_t i = 0; i < x.size(); ++i) path += std::format("{:.2f},{:.2f} ", px(x[i]), py(y[i]));
    path += "\"/>";
    svg += path;
    svg += std::format(R"(<text class="tick" x="{}" y="{}">{}</text>)", left, height - 8, htmlEscape(xLabel));
    svg += std::format(R"(<text class="tick" x="4" y="{}">{}</text>)", top_ + 8, htmlEscape(yLabel));
    svg += std::format(R"(<text class="tick" x="4" y="{}">{}</text>)", top_ + plotH, fmt(ymin));
    svg += "</svg>";
    return svg;
}

bool isSelfContained(std::string_view html, std::vector<std::string>* offenders) {
    static constexpr std::array<std::string_view, 6> kBad{"http://", "https://", "//cdn", "<script", "@import",
                                                          "url(http"};
    bool ok = true;
    for (std::string_view bad : kBad) {
        for (std::size_t at = html.find(bad); at != std::string_view::npos; at = html.find(bad, at + 1)) {
            ok = false;
            if (!offenders) return false;
            offenders->emplace_back(html.substr(at, std::min<std::size_t>(48, html.size() - at)));
        }
    }
    return ok;
}

} // namespace qlab::report
