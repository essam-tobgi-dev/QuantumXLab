#include "UI/Theory/TheoryIndex.hpp"
#include "Core/Json.hpp"
#include "Core/Paths.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace qlab::ui::theory {
namespace {
std::string lower(std::string_view s) {
    std::string o(s);
    for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}
std::string snippetAround(const std::string& text, const std::string& needleLower, std::size_t width = 120) {
    std::string lo = lower(text);
    std::size_t p = lo.find(needleLower);
    if (p == std::string::npos) return text.substr(0, std::min(text.size(), width));
    std::size_t b = p > width / 2 ? p - width / 2 : 0;
    std::size_t len = std::min(width, text.size() - b);
    std::string s = text.substr(b, len);
    if (b > 0) s = "…" + s;
    if (b + len < text.size()) s += "…";
    return s;
}
} // namespace

std::filesystem::path TheoryIndex::defaultDir() {
    if (const char* e = std::getenv("QXL_THEORY_DIR")) return e;
    return core::assetDir().parent_path() / "docs" / "theory";
}

void TheoryIndex::add(TheoryDocument doc) {
    std::string id = doc.id;
    docs_.insert_or_assign(std::move(id), std::move(doc));
}

Result<TheoryIndex> TheoryIndex::load(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        return fail(ErrorCode::Ui_ + 16, "theory directory not found: " + dir.string());
    TheoryIndex idx;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".md") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto& p : files) {
        auto text = core::readTextFile(p);
        if (!text) return std::unexpected(text.error());
        idx.add(parseTheoryMarkdown(*text, p.string()));
    }
    if (idx.docs_.empty()) return fail(ErrorCode::Ui_ + 17, "no theory documents in " + dir.string());
    return idx;
}

const TheoryDocument* TheoryIndex::document(std::string_view id) const {
    auto it = docs_.find(id);
    return it == docs_.end() ? nullptr : &it->second;
}

std::vector<std::string> TheoryIndex::documentIds() const {
    std::vector<std::string> ids;
    ids.reserve(docs_.size());
    for (const auto& [k, v] : docs_) ids.push_back(k);
    return ids;
}

std::optional<Location> TheoryIndex::resolve(std::string_view ref) const {
    std::string doc(ref), anchor;
    if (auto h = doc.find('#'); h != std::string::npos) {
        anchor = doc.substr(h + 1);
        doc = doc.substr(0, h);
    }
    // Accept "T05", "T05-superconducting-qubits.md" and paths.
    LinkRef lr = classifyLink(doc.empty() ? ref : std::string_view(doc));
    std::string id = lr.kind == LinkKind::Theory ? lr.doc : doc;
    const TheoryDocument* d = document(id);
    if (!d) return std::nullopt;
    Location loc;
    loc.docId = d->id;
    loc.doc = d;
    if (anchor.empty()) return loc;
    const HeadingRef* h = d->heading(anchor);
    if (!h) return std::nullopt;
    loc.heading = h;
    loc.blockIndex = h->blockIndex;
    return loc;
}

std::optional<Location> TheoryIndex::resolve(const LinkRef& link) const {
    if (link.kind != LinkKind::Theory) return std::nullopt;
    return resolve(link.anchor.empty() ? link.doc : link.doc + "#" + link.anchor);
}

std::vector<SearchHit> TheoryIndex::search(std::string_view query, std::size_t maxHits) const {
    std::string q = lower(query);
    std::vector<SearchHit> hits;
    if (q.empty()) return hits;
    for (const auto& [id, doc] : docs_) {
        std::string curAnchor, curHeading;
        for (std::size_t bi = 0; bi < doc.blocks.size(); ++bi) {
            const Block& b = doc.blocks[bi];
            if (b.kind == BlockKind::Heading) {
                curAnchor = b.anchor;
                curHeading = b.plain;
            }
            std::string hay;
            switch (b.kind) {
            case BlockKind::Heading:
            case BlockKind::Paragraph:
            case BlockKind::Quote: hay = b.plain; break;
            case BlockKind::Code: hay = b.plain; break;
            case BlockKind::DisplayMath: hay = b.latex; break;
            case BlockKind::List:
                for (const auto& it : b.items) { hay += spansToPlainText(it.spans); hay += ' '; }
                break;
            case BlockKind::Table:
                for (const auto& row : b.rows)
                    for (const auto& c : row) { hay += c.plain; hay += ' '; }
                break;
            default: break;
            }
            if (hay.empty()) continue;
            if (lower(hay).find(q) == std::string::npos) continue;
            SearchHit h;
            h.docId = id;
            h.headingAnchor = curAnchor;
            h.headingText = curHeading;
            h.blockIndex = bi;
            h.snippet = snippetAround(hay, q);
            h.score = b.kind == BlockKind::Heading ? 100 : b.kind == BlockKind::Paragraph ? 50 : 20;
            hits.push_back(std::move(h));
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) { return a.score > b.score; });
    if (hits.size() > maxHits) hits.resize(maxHits);
    return hits;
}

} // namespace qlab::ui::theory
