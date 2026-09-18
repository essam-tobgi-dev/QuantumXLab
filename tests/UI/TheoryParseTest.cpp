// Spec 20 §5 — Markdown+LaTeX block/inline parsing and the anchor slug rule.
#include <catch2/catch_test_macros.hpp>
#include "UI/Theory/TheoryIndex.hpp"
#include <algorithm>

using namespace qlab::ui::theory;

namespace {
const char* kFixture = R"MD(# T99 — Fixture Document

An intro paragraph with **bold**, *italic*, `code`, inline math $E = mc^2$ and a
[theory link](T05-superconducting-qubits.md#6.3-dispersive-readout).

## 1. First section

Text before the equation.

$$
\hbar\omega_{01} = \sqrt{8E_JE_C} - E_C \tag{1.1}
$$

### 1.1 A subsection

| Quantity | Symbol | Value |
|----------|:------:|------:|
| Frequency | $f_{01}$ | 4.8 GHz |
| Lifetime | $T_1$ | 150 µs |

- first item
- second item with $\chi$
- third item

1. ordered one
2. ordered two

> A quoted remark about the model.

```cpp
int main() { return 0; }
```

---

## 2. Links and refs

See [spec 14](spec:14), the [attenuator](component:attenuator), equation
[eq](eq:transmon_hamiltonian), the [site](https://example.org) and [this](#1-first-section).
)MD";
} // namespace

TEST_CASE("slug rule matches the shipped anchors") {
    REQUIRE(slugify("## 5.1 Direct capacitive coupling") == "5.1-direct-capacitive-coupling");
    REQUIRE(slugify("5.2 Tunable coupler") == "5.2-tunable-coupler");
    REQUIRE(slugify("6.3 Dispersive readout") == "6.3-dispersive-readout");
    REQUIRE(slugify("6.4 Purcell decay and the Purcell filter") == "6.4-purcell-decay-and-the-purcell-filter");
    REQUIRE(slugify("1.1 RF confinement and the pseudopotential") == "1.1-rf-confinement-and-the-pseudopotential");
    REQUIRE(slugify("3. Laser–ion interaction in the Lamb–Dicke regime") ==
            "3-laser-ion-interaction-in-the-lamb-dicke-regime");
    REQUIRE(slugify("7.1 Fluorescence") == "7.1-fluorescence");
    REQUIRE(slugify("1.2 Cooling power") == "1.2-cooling-power");
    REQUIRE(slugify("1.3 The circulation loop") == "1.3-the-circulation-loop");
    // Punctuation dropped, dash-likes and spaces collapsed, trailing dot after a bare number gone.
    REQUIRE(slugify("9. Typical parameters (2023–2025 systems)") == "9-typical-parameters-2023-2025-systems");
    REQUIRE(slugify("7. Sampling, quantization, timing") == "7-sampling-quantization-timing");
    REQUIRE(slugify("11. Flux bias lines, filters, and crosstalk") == "11-flux-bias-lines-filters-and-crosstalk");
    REQUIRE(slugify("10. Scaling beyond one chain (QCCD)") == "10-scaling-beyond-one-chain-qccd");
    REQUIRE(slugify("  Where this is used  ") == "where-this-is-used");
}

TEST_CASE("link classification covers every target form") {
    REQUIRE(classifyLink("T05-superconducting-qubits.md#6.3-dispersive-readout").kind == LinkKind::Theory);
    REQUIRE(classifyLink("T05-superconducting-qubits.md#6.3-dispersive-readout").doc == "T05");
    REQUIRE(classifyLink("T05-superconducting-qubits.md#6.3-dispersive-readout").anchor == "6.3-dispersive-readout");
    REQUIRE(classifyLink("spec:14").kind == LinkKind::Spec);
    REQUIRE(classifyLink("spec:14").id == "14");
    REQUIRE(classifyLink("spec:14#5").anchor == "5");
    REQUIRE(classifyLink("component:attenuator").kind == LinkKind::Component);
    REQUIRE(classifyLink("component:attenuator").id == "attenuator");
    REQUIRE(classifyLink("eq:transmon_hamiltonian").kind == LinkKind::Equation);
    REQUIRE(classifyLink("eq:transmon_hamiltonian").id == "transmon_hamiltonian");
    REQUIRE(classifyLink("https://example.org").kind == LinkKind::External);
    REQUIRE(classifyLink("#1-first-section").kind == LinkKind::Internal);
    REQUIRE(classifyLink("../specs/09-hardware-models.md").kind == LinkKind::File);
}

TEST_CASE("fixture document parses every block type") {
    TheoryDocument d = parseTheoryMarkdown(kFixture, "T99-fixture.md");
    REQUIRE(d.id == "T99");
    REQUIRE(d.title == "T99 — Fixture Document");

    auto countOf = [&](BlockKind k) {
        std::size_t n = 0;
        for (const auto& b : d.blocks)
            if (b.kind == k) ++n;
        return n;
    };
    REQUIRE(countOf(BlockKind::Heading) == 4);
    REQUIRE(countOf(BlockKind::DisplayMath) == 1);
    REQUIRE(countOf(BlockKind::Table) == 1);
    REQUIRE(countOf(BlockKind::Code) == 1);
    REQUIRE(countOf(BlockKind::Quote) == 1);
    REQUIRE(countOf(BlockKind::Rule) == 1);
    REQUIRE(countOf(BlockKind::List) == 2);
    REQUIRE(countOf(BlockKind::Paragraph) >= 2);

    // Headings and anchors.
    REQUIRE(d.headings.size() == 4);
    REQUIRE(d.headings[1].anchor == "1-first-section");
    REQUIRE(d.headings[2].anchor == "1.1-a-subsection");
    REQUIRE(d.heading("1.1-a-subsection") != nullptr);
    REQUIRE(d.heading("nope") == nullptr);

    // Inline runs of the intro paragraph.
    const Block* intro = nullptr;
    for (const auto& b : d.blocks)
        if (b.kind == BlockKind::Paragraph) { intro = &b; break; }
    REQUIRE(intro);
    bool bold = false, ital = false, code = false, math = false, link = false;
    for (const auto& s : intro->spans) {
        bold |= s.bold && s.text == "bold";
        ital |= s.italic && s.text == "italic";
        code |= s.code && s.text == "code";
        math |= s.math && s.text == "E = mc^2";
        if (s.link.kind == LinkKind::Theory) { link = true; REQUIRE(s.link.doc == "T05"); }
    }
    REQUIRE(bold);
    REQUIRE(ital);
    REQUIRE(code);
    REQUIRE(math);
    REQUIRE(link);

    // Display math keeps the body and strips \tag.
    for (const auto& b : d.blocks)
        if (b.kind == BlockKind::DisplayMath) {
            REQUIRE(b.tag == "1.1");
            REQUIRE(b.latex.find("\\tag") == std::string::npos);
            REQUIRE(b.latex.find("sqrt{8E_JE_C}") != std::string::npos);
        }

    // Table: header + 2 rows, 3 columns, alignment row parsed.
    for (const auto& b : d.blocks)
        if (b.kind == BlockKind::Table) {
            REQUIRE(b.rows.size() == 3);
            REQUIRE(b.rows[0].size() == 3);
            REQUIRE(b.align.size() == 3);
            REQUIRE(b.align[1] == ColumnAlign::Center);
            REQUIRE(b.align[2] == ColumnAlign::Right);
            REQUIRE(b.rows[0][0].plain == "Quantity");
            // A '|' must not split inline math; the cell keeps its math span.
            bool hasMath = false;
            for (const auto& s : b.rows[1][1].spans) hasMath |= s.math;
            REQUIRE(hasMath);
        }

    // Lists.
    std::vector<const Block*> lists;
    for (const auto& b : d.blocks)
        if (b.kind == BlockKind::List) lists.push_back(&b);
    REQUIRE(lists.size() == 2);
    REQUIRE_FALSE(lists[0]->ordered);
    REQUIRE(lists[0]->items.size() == 3);
    REQUIRE(lists[1]->ordered);
    REQUIRE(lists[1]->items.size() == 2);

    // Code fence keeps the language and body.
    for (const auto& b : d.blocks)
        if (b.kind == BlockKind::Code) {
            REQUIRE(b.language == "cpp");
            REQUIRE(b.plain.find("int main()") != std::string::npos);
        }

    // Every link form in the last paragraph.
    std::vector<LinkKind> kinds;
    for (const auto& b : d.blocks)
        for (const auto& s : b.spans)
            if (s.link.kind != LinkKind::None) kinds.push_back(s.link.kind);
    auto has = [&](LinkKind k) { return std::find(kinds.begin(), kinds.end(), k) != kinds.end(); };
    REQUIRE(has(LinkKind::Spec));
    REQUIRE(has(LinkKind::Component));
    REQUIRE(has(LinkKind::Equation));
    REQUIRE(has(LinkKind::External));
    REQUIRE(has(LinkKind::Internal));
}

TEST_CASE("snake_case underscores are literal, escaped dollars stay text") {
    auto spans = parseInline("field name_with_underscores and \\$5 cost");
    std::string joined;
    for (const auto& s : spans) { REQUIRE_FALSE(s.italic); joined += s.text; }
    REQUIRE(joined.find("name_with_underscores") != std::string::npos);
    REQUIRE(joined.find("$5") != std::string::npos);
}
