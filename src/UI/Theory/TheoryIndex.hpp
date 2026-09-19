#pragma once
// Spec 20 §5 — the loaded theory corpus: anchor resolution and search for the Theory Browser.
#include "UI/Theory/TheoryDocument.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace qlab::ui::theory {

struct Location {
    std::string docId;
    std::size_t blockIndex = 0;
    const TheoryDocument* doc = nullptr;
    const HeadingRef* heading = nullptr; // null when only the document resolved
};

struct SearchHit {
    std::string docId;
    std::string headingAnchor; // nearest enclosing heading
    std::string headingText;
    std::size_t blockIndex = 0;
    std::string snippet;
    int score = 0;
};

class TheoryIndex {
  public:
    // Load every *.md in `dir` (non-recursive). `docs/theory/README.md` is loaded too; its id
    // is "README" and it never collides with a Txx id.
    static Result<TheoryIndex> load(const std::filesystem::path& dir);
    // Default corpus location: <assetDir>/../docs/theory, overridable with QXL_THEORY_DIR.
    static std::filesystem::path defaultDir();

    void add(TheoryDocument doc);

    const TheoryDocument* document(std::string_view id) const;
    std::vector<std::string> documentIds() const;
    std::size_t size() const { return docs_.size(); }

    // "T05#6.3-dispersive-readout", "T05", or a LinkRef of kind Theory.
    std::optional<Location> resolve(std::string_view ref) const;
    std::optional<Location> resolve(const LinkRef& link) const;

    // Case-insensitive term search over headings and paragraph text; headings score higher.
    std::vector<SearchHit> search(std::string_view query, std::size_t maxHits = 50) const;

  private:
    std::map<std::string, TheoryDocument, std::less<>> docs_;
};

} // namespace qlab::ui::theory
