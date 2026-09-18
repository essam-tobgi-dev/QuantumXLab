// Spec 20 §3/§5 — the shipped theory corpus must parse, and every display equation in it must
// lay out in the fallback renderer. Also checks that every `theory` anchor used by the lab
// component descriptors resolves (spec 17 §4).
#include <catch2/catch_test_macros.hpp>
#include "Core/Json.hpp"
#include "Core/Paths.hpp"
#include "UI/Math/BasicMathRenderer.hpp"
#include "UI/Theory/TheoryIndex.hpp"
#include "MathTestFont.hpp"
#include <filesystem>
#include <iostream>
#include <functional>
#include <map>
#include <set>

using namespace qlab;
using namespace qlab::ui;
using namespace qlab::ui::theory;

namespace {
std::filesystem::path theoryDir() { return TheoryIndex::defaultDir(); }
} // namespace

TEST_CASE("theory corpus parses and every display equation lays out") {
    auto idx = TheoryIndex::load(theoryDir());
    REQUIRE(idx);
    REQUIRE(idx->size() >= 13); // T01–T12 + README

    test::MonospaceTestFont font;
    BasicMathRenderer renderer(font, 4096);

    std::size_t totalEq = 0, warnedEq = 0, totalInline = 0, warnedInline = 0;
    std::map<std::string, std::pair<std::size_t, std::size_t>> perDoc; // id → {equations, warned}
    std::set<std::string> sampleWarnings;

    for (const auto& id : idx->documentIds()) {
        const TheoryDocument* doc = idx->document(id);
        REQUIRE(doc != nullptr);
        REQUIRE_FALSE(doc->blocks.empty());
        std::size_t eq = 0, warned = 0;
        for (const auto& b : doc->blocks) {
            if (b.kind == BlockKind::DisplayMath) {
                ++eq;
                ++totalEq;
                auto lr = renderer.render(b.latex, math::MathStyle{20.0, true});
                INFO(id << " line " << b.line << ": " << b.latex);
                REQUIRE(lr);                       // a structural failure is an error
                REQUIRE((*lr)->width > 0.0);
                if (!(*lr)->warnings.empty()) {
                    ++warned;
                    ++warnedEq;
                    for (const auto& w : (*lr)->warnings)
                        if (sampleWarnings.size() < 12) sampleWarnings.insert(id + ": " + w);
                }
            }
            // Inline math in paragraphs, headings, list items and table cells.
            auto checkSpans = [&](const std::vector<InlineSpan>& spans) {
                for (const auto& s : spans) {
                    if (!s.math || s.text.empty()) continue;
                    ++totalInline;
                    auto lr = renderer.render(s.text, math::MathStyle{18.0, false});
                    INFO(id << " line " << b.line << " inline: " << s.text);
                    REQUIRE(lr);
                    if (!(*lr)->warnings.empty()) {
                        ++warnedInline;
                        for (const auto& w : (*lr)->warnings)
                            if (sampleWarnings.size() < 12) sampleWarnings.insert(id + ": " + w);
                    }
                }
            };
            checkSpans(b.spans);
            for (const auto& it : b.items) checkSpans(it.spans);
            for (const auto& row : b.rows)
                for (const auto& c : row) checkSpans(c.spans);
        }
        perDoc[id] = {eq, warned};
    }

    std::cout << "\n[theory corpus] display equations: " << totalEq << ", warned: " << warnedEq
              << " (" << (totalEq ? 100.0 * static_cast<double>(warnedEq) / static_cast<double>(totalEq) : 0.0)
              << " %)\n[theory corpus] inline math: " << totalInline << ", warned: " << warnedInline << "\n";
    for (const auto& [id, s] : perDoc)
        std::cout << "  " << id << ": " << s.first << " equations, " << s.second << " warned\n";
    for (const auto& w : sampleWarnings) std::cout << "  warning sample — " << w << "\n";
    std::cout.flush();

    REQUIRE(totalEq > 200); // the corpus really does carry its equations
    // Command coverage target of spec 20 §3: under 5 % of blocks may warn.
    REQUIRE(static_cast<double>(warnedEq) < 0.05 * static_cast<double>(totalEq));
    REQUIRE(static_cast<double>(warnedInline) < 0.05 * static_cast<double>(totalInline));
}

TEST_CASE("component descriptors' theory anchors all resolve") {
    auto idx = TheoryIndex::load(theoryDir());
    REQUIRE(idx);
    auto compDir = core::assetDir() / "Lab" / "Components";
    if (!std::filesystem::is_directory(compDir)) {
        WARN("Assets/Lab/Components not present; skipping anchor cross-check");
        return;
    }
    std::set<std::string> anchors;
    std::size_t files = 0;
    std::function<void(const core::Json&)> walk = [&](const core::Json& j) {
        if (j.is_object()) {
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (it.key() == "theory") {
                    if (it->is_string()) anchors.insert(it->get<std::string>());
                    else if (it->is_array())
                        for (const auto& a : *it)
                            if (a.is_string()) anchors.insert(a.get<std::string>());
                } else walk(*it);
            }
        } else if (j.is_array()) {
            for (const auto& v : j) walk(v);
        }
    };
    for (const auto& e : std::filesystem::directory_iterator(compDir)) {
        auto f = e.path() / "component.json";
        if (!std::filesystem::exists(f)) continue;
        ++files;
        auto text = core::readTextFile(f);
        REQUIRE(text);
        core::Json j = core::Json::parse(*text, nullptr, false);
        REQUIRE_FALSE(j.is_discarded());
        walk(j);
    }
    REQUIRE(files > 0);
    REQUIRE(anchors.size() > 10);
    std::vector<std::string> unresolved;
    for (const auto& a : anchors)
        if (!idx->resolve(a)) unresolved.push_back(a);
    for (const auto& u : unresolved) std::cout << "  unresolved anchor: " << u << "\n";
    INFO("checked " << anchors.size() << " distinct anchors from " << files << " components");
    REQUIRE(unresolved.empty());
}

TEST_CASE("anchors named in the spec resolve to their headings") {
    auto idx = TheoryIndex::load(theoryDir());
    REQUIRE(idx);
    const char* kRefs[] = {
        "T05#5.1-direct-capacitive-coupling", "T05#5.2-tunable-coupler",
        "T05#6.3-dispersive-readout",         "T05#6.4-purcell-decay-and-the-purcell-filter",
        "T06#1.1-rf-confinement-and-the-pseudopotential", "T06#1.3-normal-modes",
        "T06#3-laser-ion-interaction-in-the-lamb-dicke-regime", "T06#7.1-fluorescence",
        "T08#1.2-cooling-power", "T08#1.3-the-circulation-loop",
    };
    for (const char* r : kRefs) {
        INFO(r);
        auto loc = idx->resolve(r);
        REQUIRE(loc.has_value());
        REQUIRE(loc->heading != nullptr);
        REQUIRE(loc->doc != nullptr);
        REQUIRE(loc->doc->blocks[loc->blockIndex].kind == BlockKind::Heading);
    }
    // A document alone resolves; a bad anchor does not.
    REQUIRE(idx->resolve("T05").has_value());
    REQUIRE(idx->resolve("T05#no-such-heading") == std::nullopt);
    REQUIRE(idx->resolve("T99#x") == std::nullopt);
    // Full file-name form works too.
    REQUIRE(idx->resolve("T05-superconducting-qubits.md#6.3-dispersive-readout").has_value());
}

TEST_CASE("search finds headings and body text") {
    auto idx = TheoryIndex::load(theoryDir());
    REQUIRE(idx);
    auto hits = idx->search("dispersive readout", 20);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits.front().score >= 50);
    bool inT05 = false;
    for (const auto& h : hits) inT05 |= h.docId == "T05";
    REQUIRE(inT05);
    REQUIRE_FALSE(hits.front().snippet.empty());
    REQUIRE(idx->search("").empty());
    REQUIRE(idx->search("zzzznotpresentzzz").empty());
}
