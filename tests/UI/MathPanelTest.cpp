// Spec 20 §1/§5/§7 — the ImGui backend of the LaTeX engine and the interactive equation widget:
// metrics from the rasterised face, painting into a draw list, hit testing over the symbol boxes,
// the layout cache, and the corpora the Inspector, the tooltips and the editor read.
#include "UI/Widgets/EquationView.hpp"
#include "Data/Fidelity.hpp"
#include "UI/Widgets/Equations.hpp"
#include "UI/Widgets/MathImGui.hpp"
#include "UiHarness.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using namespace qlab;
using namespace qlab::ui;
using Catch::Approx;

TEST_CASE("Theory assets: equations, gates and assumptions load and cross-reference") {
    const auto assets = TheoryAssets::load();
    if (!assets) SKIP("Assets/Theory is not available: " + assets.error().message);
    CHECK(assets->equations().size() >= 100);
    CHECK(assets->gates().size() >= 40);
    CHECK(assets->assumptionCount() >= 20);

    const EquationDoc* lc = assets->equation("lc_hamiltonian");
    REQUIRE(lc != nullptr);
    CHECK(lc->latex.find("\\frac") != std::string::npos);
    CHECK_FALSE(lc->plain.empty());
    CHECK(lc->theory.rfind("T05#", 0) == 0);
    REQUIRE(lc->terms.size() >= 4);
    CHECK(lc->term("C") != nullptr);
    CHECK(lc->term("C")->unit == "F");
    CHECK(lc->term("nope") == nullptr);
    CHECK(assets->equation("no_such_equation") == nullptr);

    const GateDocEntry* cx = assets->gate("cx");
    REQUIRE(cx != nullptr);
    CHECK(cx->qubits == 2);
    CHECK_FALSE(cx->matrixLatex.empty());
    const GateDocEntry* u = assets->gate("U");
    REQUIRE(u != nullptr);
    CHECK(u->params.size() == 3);

    // Every assumption an equation names must resolve (spec 20 §5 renders it under the equation).
    std::size_t withAssumptions = 0;
    for (const EquationDoc& e : assets->equations())
        for (const std::string& key : e.assumptions) {
            ++withAssumptions;
            INFO("equation " << e.id << ", assumption " << key);
            CHECK_FALSE(assets->assumption(key).empty());
        }
    CHECK(withAssumptions > 0);
}

TEST_CASE("Math backend: the ImGui face reports sane TeX metrics") {
    test::UiHarness ui;
    if (!ui.ready()) SKIP("the pinned fonts are not available");
    const ImGuiMathFont font(ui.resources().fonts);
    REQUIRE(font.valid());

    const math::GlyphMetrics x = font.metrics("x", 18.0, math::GlyphStyle::Italic);
    CHECK(x.advance > 4.0);
    CHECK(x.advance < 18.0);
    CHECK(x.ascent > 4.0);
    CHECK(x.ascent < 18.0);
    // `x` has no descender; `y` does.
    CHECK(font.metrics("y", 18.0, math::GlyphStyle::Italic).descent > x.descent);
    // A capital reaches higher than the x-height.
    CHECK(font.metrics("H", 18.0, math::GlyphStyle::Upright).ascent > x.ascent);
    // Metrics scale with the requested size.
    const math::GlyphMetrics big = font.metrics("x", 36.0, math::GlyphStyle::Italic);
    CHECK(big.advance == Approx(2.0 * x.advance).epsilon(0.05));
    // The TeX constants follow the face rather than the 0.45 fallback.
    CHECK(font.xHeight(18.0) > 5.0);
    CHECK(font.xHeight(18.0) < 18.0);
    CHECK(font.axisHeight(18.0) == Approx(0.5 * font.xHeight(18.0)));
    CHECK(font.ruleThickness(18.0) >= 1.0);
    // The LaTeX face (spec 20 §1): one OpenType math font, styles as code points — an italic
    // `x` is MATHEMATICAL ITALIC SMALL X, bold is its bold twin, `h` the Planck-constant italic.
    REQUIRE(font.hasMathFace());
    CHECK(font.face(math::GlyphStyle::Bold) == font.face(math::GlyphStyle::Italic));
    CHECK(ImGuiMathFont::variant(U'x', math::GlyphStyle::Italic) == 0x1D465);
    CHECK(ImGuiMathFont::variant(U'x', math::GlyphStyle::Bold) == 0x1D431);
    CHECK(ImGuiMathFont::variant(U'h', math::GlyphStyle::Italic) == 0x210E);
    CHECK(ImGuiMathFont::variant(0x3B1, math::GlyphStyle::Italic) == 0x1D6FC);
    CHECK(ImGuiMathFont::variant(U'x', math::GlyphStyle::Upright) == U'x');
    CHECK(font.styled("x", math::GlyphStyle::Italic) == "\xF0\x9D\x91\xA5");
    CHECK(font.styled("cool", math::GlyphStyle::Italic) == "cool");   // a run stays upright text
    // The face carries the repertoire an equation needs: big operators, accents, delimiters.
    for (ImWchar cp : {ImWchar(0x222B), ImWchar(0x2211), ImWchar(0x02D9), ImWchar(0x27E8), ImWchar(0x27E9), ImWchar(0x210F), ImWchar(0x1D465), ImWchar(0x1D6FC)}) {
        INFO("code point U+" << std::hex << cp);
        CHECK(font.face(math::GlyphStyle::Upright)->FindGlyphNoFallback(cp) != nullptr);
    }
    // An italic letter is wider than the upright one on this face (the italic has a slant and
    // an italic correction), and an empty run has no metrics.
    CHECK(font.metrics("f", 18.0, math::GlyphStyle::Italic).advance != Approx(font.metrics("f", 18.0, math::GlyphStyle::Upright).advance));
    CHECK(font.metrics("", 18.0, math::GlyphStyle::Italic).advance == Approx(0.0));
}

TEST_CASE("Math backend: an equation lays out, paints into a draw list and hit-tests") {
    test::UiHarness ui;
    if (!ui.ready()) SKIP("the pinned fonts are not available");
    MathRenderers& math = *ui.context().math;
    REQUIRE(math.ready());

    const auto laid = math.renderer().render("E = mc^2", math::MathStyle{18.0, true});
    REQUIRE(laid.has_value());
    const math::LayoutResult& lr = **laid;
    CHECK(lr.errors.empty());
    CHECK(lr.width > 30.0);
    CHECK(lr.height > 5.0);

    // The cache is keyed on (latex, size, style): the same request is served from it.
    const std::size_t hits = math.renderer().cacheHits();
    const auto again = math.renderer().render("E = mc^2", math::MathStyle{18.0, true});
    REQUIRE(again.has_value());
    CHECK(again->get() == laid->get());
    CHECK(math.renderer().cacheHits() == hits + 1);

    // Every symbol has at least one hit box (spec 20 §9) and hit testing finds it.
    const std::vector<math::SymbolHit> rects = math::symbolRects(lr, 0.0, lr.height);
    CHECK(rects.size() >= 4);                 // E = m c 2
    const auto findSymbol = [&](std::string_view text) {
        return std::find_if(rects.begin(), rects.end(), [&](const math::SymbolHit& h) { return h.text == text; });
    };
    const auto e = findSymbol("E");
    REQUIRE(e != rects.end());
    const auto hit = math::hitTestMath(lr, 0.0, lr.height, e->x + e->w * 0.5, e->y + e->h * 0.5);
    REQUIRE(hit.has_value());
    CHECK(hit->text == "E");
    CHECK_FALSE(math::hitTestMath(lr, 0.0, lr.height, -50.0, -50.0).has_value());

    // Painting emits geometry into the frame's draw list.
    int before = 0, after = 0;
    ui.frame([&] {
        ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
        ImGui::Begin("math", nullptr, ImGuiWindowFlags_NoSavedSettings);
        before = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        // Inside the window's clip rectangle, or every glyph would be culled.
        ImGuiMathCanvas canvas(ImGui::GetWindowDrawList(), math.font(), ImGui::GetCursorScreenPos(),
                               IM_COL32(255, 255, 255, 255));
        math::paintMath(lr, canvas, 0.0, lr.height, math.font());
        after = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        ImGui::End();
    });
    CHECK(after > before);

    // A fraction and a radical exercise the rule and the stretched delimiter paths.
    const auto frac = math.renderer().render("\\frac{\\hbar\\omega}{2} + \\sqrt{E_J E_C}", math::MathStyle{18.0, true});
    REQUIRE(frac.has_value());
    CHECK((*frac)->errors.empty());
    CHECK((*frac)->height + (*frac)->depth > lr.height + lr.depth);   // taller than the one-line formula
}

TEST_CASE("EquationView: hover symbols, live values and the theory link (spec 20 §5)") {
    test::UiHarness ui;
    if (!ui.ready()) SKIP("the pinned fonts are not available");
    const TheoryAssets* assets = ui.context().assets;
    REQUIRE(assets != nullptr);
    const EquationDoc* doc = assets->equation("lc_hamiltonian");
    if (doc == nullptr) SKIP("Assets/Theory/equations.json is not available");

    EquationView view;
    view.setDocument(doc);
    CHECK(view.latex() == doc->latex);
    CHECK(view.document() == doc);

    std::vector<TermValue> values;
    values.push_back(TermValue{"C", 65e-15, "F", data::FidelityClass::Model, false, true});
    values.push_back(TermValue{"L", 0.0, "H", data::FidelityClass::Model, false, false});   // unavailable
    view.setValues(values);
    view.setWithValues(true);

    // The size is known before drawing, so a panel can lay itself out around the equation.
    const ImVec2 size = view.size(ui.context());
    CHECK(size.x > 40.0f);
    CHECK(size.y > 10.0f);
    // A symbol of the equation is under its own box.
    const auto laid = ui.context().math->renderer().render(doc->latex, math::MathStyle{
                                                                          static_cast<double>(size.y), true});
    CHECK(laid.has_value());

    std::string opened;
    ui.context().cmd.openTheory = [&opened](std::string_view anchor) { opened = std::string(anchor); };
    int vertices = 0;
    ui.frame([&] {
        ImGui::SetNextWindowSize(ImVec2(800.0f, 400.0f));
        ImGui::Begin("equation", nullptr, ImGuiWindowFlags_NoSavedSettings);
        view.draw(ui.context());
        vertices = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        ImGui::End();
    });
    CHECK(vertices > 0);
    CHECK(opened.empty());   // nothing was clicked

    // `symbolAt` answers over the laid-out boxes.
    const std::string symbol = view.symbolAt(ui.context(), ImVec2(size.x * 0.02f, size.y * 0.5f));
    CHECK_FALSE(symbol.empty());
    CHECK(view.symbolAt(ui.context(), ImVec2(-100.0f, -100.0f)).empty());

    // Without a math renderer the view degrades to the LaTeX source rather than crashing.
    UiContext bare;
    bare.theme = &ui.resources().theme;
    bare.fonts = &ui.resources().fonts;
    EquationView plain;
    plain.setLatex("x + y");
    CHECK(plain.size(bare).x == 0.0f);
    ui.frame([&] {
        ImGui::Begin("bare", nullptr, ImGuiWindowFlags_NoSavedSettings);
        CHECK(plain.draw(bare).empty());
        ImGui::End();
    });
}

TEST_CASE("EduTooltip and the tooltip corpus render inside a frame (spec 19 §6)") {
    test::UiHarness ui;
    if (!ui.ready()) SKIP("the pinned fonts are not available");
    const std::array<TermValue, 1> values{TermValue{"C", 65e-15, "F", data::FidelityClass::Model, false, true}};
    const auto body = [&] {
        ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
        ImGui::Begin("tooltip", nullptr, ImGuiWindowFlags_NoSavedSettings);
        eduCard(ui.context(), "lc_hamiltonian", "Node capacitance", "The capacitance of the LC mode.", values);
        ImGui::Button("hover me");
        eduTooltip(ui.context(), "lc_hamiltonian", values);   // no hover: draws nothing, asserts nothing
        ImGui::End();
    };
    ui.frame(body);   // ImGui hides a freshly created window while it sizes itself
    ui.frame(body);
    CHECK(ui.vertices() > 0);
}
