// Spec 25 §7 / spec 17 §12 — cross-reference lint of the component catalog: equation ids resolve
// in Assets/Theory/equations.json and theory anchors resolve to docs/theory headings. The anchor
// slug rule mirrors tools/gen_assets.py (`<section>-<slug>`, "where-this-is-used").
#include "Lab/Catalog.hpp"
#include <cctype>
#include <format>
#include <fstream>
#include <set>

namespace qlab::lab {

namespace {

// slugify: strip `$` math delimiters and backslashes, lowercase ASCII, collapse every run of
// non-[a-z0-9] bytes into '-', trim '-'.
std::string slugify(std::string_view text) {
    std::string out;
    bool pendingDash = false;
    for (char ch : text) {
        auto c = static_cast<unsigned char>(ch);
        if (c == '$')
            continue;
        if (c < 0x80 && std::isalnum(c)) {
            if (pendingDash && !out.empty())
                out.push_back('-');
            pendingDash = false;
            out.push_back(static_cast<char>(std::tolower(c)));
        } else {
            pendingDash = true;
        }
    }
    return out;
}

// "## 3. Cooper-pair box" / "### 6.3 Dispersive readout" → "3-cooper-pair-box" /
// "6.3-dispersive-readout".
std::optional<std::string> headingAnchor(const std::string& line) {
    if (line.rfind("## Where this is used", 0) == 0)
        return std::string("where-this-is-used");
    std::size_t hashes = 0;
    while (hashes < line.size() && line[hashes] == '#')
        ++hashes;
    if (hashes < 2 || hashes > 3)
        return std::nullopt;
    std::size_t i = hashes;
    if (i >= line.size() || !std::isspace(static_cast<unsigned char>(line[i])))
        return std::nullopt;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
        ++i;
    std::size_t numStart = i;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])))
        ++i;
    if (i == numStart)
        return std::nullopt;
    if (i + 1 < line.size() && line[i] == '.' &&
        std::isdigit(static_cast<unsigned char>(line[i + 1]))) {
        ++i;
        while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])))
            ++i;
    }
    std::string number = line.substr(numStart, i - numStart);
    if (i < line.size() && line[i] == '.')
        ++i;
    if (i >= line.size() || !std::isspace(static_cast<unsigned char>(line[i])))
        return std::nullopt;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
        ++i;
    std::string rest = line.substr(i);
    while (!rest.empty() &&
           (rest.back() == '\r' || std::isspace(static_cast<unsigned char>(rest.back()))))
        rest.pop_back();
    return number + "-" + slugify(rest);
}

std::map<std::string, std::set<std::string>> theoryAnchors(const std::filesystem::path& dir) {
    std::map<std::string, std::set<std::string>> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        std::string fn = e.path().filename().string();
        if (fn.size() < 4 || fn[0] != 'T' || !std::isdigit(static_cast<unsigned char>(fn[1])) ||
            !std::isdigit(static_cast<unsigned char>(fn[2])) || fn[3] != '-')
            continue;
        auto& set = out[fn.substr(0, 3)];
        std::ifstream in(e.path());
        for (std::string line; std::getline(in, line);)
            if (auto a = headingAnchor(line))
                set.insert(*a);
    }
    return out;
}

} // namespace

std::vector<std::string>
ComponentCatalog::lintReferences(const std::filesystem::path& equationsJson,
                                 const std::filesystem::path& theoryDir) const {
    std::vector<std::string> problems;
    std::set<std::string> equationIds;
    if (auto env = core::JsonEnvelope::load(equationsJson, "theory.equations"); !env) {
        problems.push_back("cannot read equations: " + env.error().message);
    } else if (auto eqs = env->data.find("equations"); eqs != env->data.end() && eqs->is_array()) {
        for (const auto& e : *eqs)
            if (e.contains("id") && e["id"].is_string())
                equationIds.insert(e["id"].get<std::string>());
    }
    const auto anchors = theoryAnchors(theoryDir);
    if (anchors.empty())
        problems.push_back("no theory documents under " + theoryDir.string());

    for (const auto& d : items_) {
        if (!equationIds.empty())
            for (const auto& id : d.equationIds)
                if (!equationIds.contains(id))
                    problems.push_back(std::format("{}: unknown equation id '{}'", d.id, id));
        for (const auto& a : d.theoryAnchors) {
            std::size_t hash = a.find('#');
            auto doc = hash == std::string::npos ? anchors.end() : anchors.find(a.substr(0, hash));
            if (doc == anchors.end() || !doc->second.contains(a.substr(hash + 1)))
                problems.push_back(std::format("{}: unresolved theory anchor '{}'", d.id, a));
        }
    }
    return problems;
}

} // namespace qlab::lab
