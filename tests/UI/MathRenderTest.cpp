// Spec 20 §3/§6/§7 — fallback math engine: acceptance set, layout geometry, hit-test, cache.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "UI/Math/BasicMathRenderer.hpp"
#include "UI/Math/MathParser.hpp"
#include "MathTestFont.hpp"
#include <functional>

using namespace qlab;
using namespace qlab::ui;
using namespace qlab::ui::math;
using qlab::ui::test::MonospaceTestFont;
using qlab::ui::test::RecordingCanvas;

namespace {
const MonospaceTestFont& font() {
    static MonospaceTestFont f;
    return f;
}
LayoutResult lay(std::string_view latex, bool display = true, double px = 20.0) {
    MathParser p(latex);
    ParseOutput po = p.parse();
    REQUIRE(po.root != nullptr);
    LayoutResult lr = layoutMath(*po.root, font(), MathStyle{px, display});
    lr.warnings.insert(lr.warnings.begin(), po.warnings.begin(), po.warnings.end());
    lr.errors = po.errors;
    return lr;
}
} // namespace

TEST_CASE("acceptance render set lays out without errors (spec 20 §3)") {
    // The quantum-specific acceptance list of spec 20 §3.
    const char* kSet[] = {
        R"(\hat{H} = 4E_C(\hat{n}-n_g)^2 - E_J\cos\hat{\varphi})",
        R"(\hbar\omega_{01} \simeq \sqrt{8E_JE_C} - E_C)",
        R"(\dot\rho = -\frac{i}{\hbar}[H,\rho] + \sum_k\left(L_k\rho L_k^\dagger - \tfrac12\{L_k^\dagger L_k,\rho\}\right))",
        R"(\chi = \frac{g^2}{\Delta}\frac{\alpha}{\Delta+\alpha})",
        R"(\mathcal{E}(\rho) = \sum_k K_k \rho K_k^\dagger)",
        R"(\vec{r} = (\mathrm{Tr}\,\rho X, \mathrm{Tr}\,\rho Y, \mathrm{Tr}\,\rho Z))",
        R"(|\psi\rangle = \cos(\theta/2)|0\rangle + e^{i\varphi}\sin(\theta/2)|1\rangle)",
        R"(S_p = \prod_{j\in p} Z_j, \qquad p_L \approx A\left(\frac{p}{p_{th}}\right)^{(d+1)/2})",
        R"(F(m) = A p^m + B)",
        R"(\dot{Q} = 84\,\dot{n}_3 T_{mc}^2 - \dot{Q}_0)",
        R"(n_{th}(f,T) = \frac{1}{e^{hf/k_BT} - 1})",
        R"(i\hbar\frac{\partial\psi}{\partial t} = \hat{H}\psi)",
        R"(\begin{pmatrix} 1 & 0 \\ 0 & e^{i\phi} \end{pmatrix})",
        R"(\mathrm{SNR} = |\alpha_0-\alpha_1|\sqrt{2\eta\kappa\tau})",
        R"(\int_{-\infty}^{\infty} x(t)e^{-i2\pi ft}\,dt)",
        R"(\lim_{n\to\infty}\left\|U^n - V\right\| = 0)",
        R"(T_{run} = T_{load} + N_{shots}(T_{reset}+T_{circ}+T_{ro}+T_{gap}))",
        R"(\langle\psi|\hat{O}|\psi\rangle \ge \frac{\hbar}{2})",
    };
    for (const char* s : kSet) {
        INFO(s);
        LayoutResult lr = lay(s);
        REQUIRE(lr.errors.empty());
        REQUIRE(lr.width > 0.0);
        REQUIRE(lr.height + lr.depth > 0.0);
        if (!lr.warnings.empty()) { INFO("warning: " << lr.warnings.front()); }
        REQUIRE(lr.warnings.empty());
    }
}

TEST_CASE("fraction places numerator above denominator with a rule between") {
    LayoutResult lr = lay(R"(\frac{a}{b})");
    RecordingCanvas cv;
    paintMath(lr, cv, 0.0, 0.0, font());
    const auto* a = cv.find("a");
    const auto* b = cv.find("b");
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(a->baselineY < b->baselineY);          // y grows downward
    REQUIRE(cv.lines.size() >= 1);                 // the fraction rule
    double ruleY = cv.lines.front().y0;
    REQUIRE(a->baselineY < ruleY);
    REQUIRE(b->baselineY > ruleY);
    REQUIRE(lr.height > 0.0);
    REQUIRE(lr.depth > 0.0);
}

TEST_CASE("superscripts rise, subscripts fall, and both shrink") {
    LayoutResult lr = lay("x^2_k");
    RecordingCanvas cv;
    paintMath(lr, cv, 0.0, 0.0, font());
    const auto* x = cv.find("x");
    const auto* two = cv.find("2");
    const auto* k = cv.find("k");
    REQUIRE(x);
    REQUIRE(two);
    REQUIRE(k);
    REQUIRE(two->baselineY < x->baselineY);
    REQUIRE(k->baselineY > x->baselineY);
    REQUIRE(two->size < x->size);
    REQUIRE(k->size < x->size);
    REQUIRE(two->size == Catch::Approx(x->size * 0.7));
}

TEST_CASE("nested scripts use scriptscript size") {
    LayoutResult lr = lay("e^{x^2}");
    RecordingCanvas cv;
    paintMath(lr, cv, 0.0, 0.0, font());
    const auto* e = cv.find("e");
    const auto* two = cv.find("2");
    REQUIRE(e);
    REQUIRE(two);
    REQUIRE(two->size == Catch::Approx(e->size * 0.5));
}

TEST_CASE("matrix environment lays out rows and columns") {
    MathParser p(R"(\begin{pmatrix} a & b \\ c & d \end{pmatrix})");
    ParseOutput po = p.parse();
    REQUIRE(po.root);
    // Find the Matrix node.
    const MathNode* m = nullptr;
    std::function<void(const MathNode&)> walk = [&](const MathNode& n) {
        if (n.kind == NodeKind::Matrix) m = &n;
        for (const auto& c : n.children)
            if (c) walk(*c);
    };
    walk(*po.root);
    REQUIRE(m != nullptr);
    REQUIRE(m->rows == 2);
    REQUIRE(m->cols == 2);

    LayoutResult lr = lay(R"(\begin{pmatrix} a & b \\ c & d \end{pmatrix})");
    RecordingCanvas cv;
    paintMath(lr, cv, 0.0, 0.0, font());
    const auto* a = cv.find("a");
    const auto* b = cv.find("b");
    const auto* c = cv.find("c");
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(c);
    REQUIRE(b->x > a->x);            // same row, next column
    REQUIRE(c->baselineY > a->baselineY); // next row is lower
    REQUIRE(cv.glyphs.size() >= 4);
}

TEST_CASE("delimiters grow with their content") {
    LayoutResult small = lay(R"(\left(x\right))");
    LayoutResult big = lay(R"(\left(\frac{\frac{a}{b}}{\frac{c}{d}}\right))");
    REQUIRE(big.height + big.depth > (small.height + small.depth) * 1.5);
    RecordingCanvas cv;
    paintMath(big, cv, 0.0, 0.0, font());
    bool scaled = false;
    for (const auto& g : cv.glyphs)
        if (g.text == "(" && g.size > 20.0 * 1.2) scaled = true;
    REQUIRE(scaled);
}

TEST_CASE("big operators take limits in display style and scripts inline") {
    LayoutResult disp = lay(R"(\sum_{k=0}^{n} a_k)", true);
    LayoutResult inl = lay(R"(\sum_{k=0}^{n} a_k)", false);
    REQUIRE(disp.height + disp.depth > inl.height + inl.depth);
    REQUIRE(inl.width > disp.width * 0.9); // inline is wider relative to its height
    RecordingCanvas cv;
    paintMath(disp, cv, 0.0, 0.0, font());
    const auto* sum = cv.find("∑");
    const auto* n = cv.find("n");
    REQUIRE(sum);
    REQUIRE(n);
    REQUIRE(n->baselineY < sum->baselineY); // upper limit sits above
}

TEST_CASE("unknown commands warn but still render") {
    LayoutResult lr = lay(R"(a + \frobnicate{b})");
    REQUIRE(lr.errors.empty());
    REQUIRE_FALSE(lr.warnings.empty());
    REQUIRE(lr.width > 0.0);
}

TEST_CASE("hit test finds the symbol painted at a point") {
    const char* src = R"(E_J \cos\varphi)";
    LayoutResult lr = lay(src);
    auto rects = symbolRects(lr, 10.0, 40.0);
    REQUIRE(rects.size() >= 3);
    for (const auto& r : rects) {
        auto hit = hitTestMath(lr, 10.0, 40.0, r.x + r.w / 2, r.y + r.h / 2);
        REQUIRE(hit.has_value());
        REQUIRE(hit->text == r.text);
        // The recorded source range must point back into the LaTeX string.
        REQUIRE(hit->src.end <= std::string_view(src).size());
        REQUIRE(hit->src.begin <= hit->src.end);
    }
    REQUIRE_FALSE(hitTestMath(lr, 10.0, 40.0, -50.0, -50.0).has_value());
}

TEST_CASE("renderer caches layouts and evicts least-recently used") {
    BasicMathRenderer r(font(), 2);
    MathStyle st{18.0, true};
    auto a1 = r.render("a+b", st);
    REQUIRE(a1);
    REQUIRE(r.cacheHits() == 0);
    auto a2 = r.render("a+b", st);
    REQUIRE(a2);
    REQUIRE(r.cacheHits() == 1);
    REQUIRE(a1->get() == a2->get()); // same shared layout
    REQUIRE(r.render("c+d", st));
    REQUIRE(r.render("e+f", st)); // evicts "a+b"
    REQUIRE(r.cacheSize() == 2);
    REQUIRE(r.render("a+b", st));
    REQUIRE(r.cacheHits() == 1); // was evicted, so no new hit
    // Style is part of the key.
    REQUIRE(r.render("a+b", MathStyle{24.0, true}));
    r.clearCache();
    REQUIRE(r.cacheSize() == 0);
}

TEST_CASE("plain text of a painted formula preserves symbol order") {
    LayoutResult lr = lay(R"(\alpha + \beta)");
    RecordingCanvas cv;
    paintMath(lr, cv, 0.0, 0.0, font());
    REQUIRE(cv.plain() == "α+β");
}

TEST_CASE("the ASCII hyphen of math mode is set as the minus sign, as TeX does") {
    using namespace qlab::ui::math;
    MathParser pm("a - b");
    ParseOutput parsed = pm.parse();
    REQUIRE(parsed.root != nullptr);
    bool minus = false, hyphen = false;
    std::function<void(const MathNode&)> walk = [&](const MathNode& n) {
        if (n.kind == NodeKind::Symbol && n.text == "\u2212") minus = true;
        if (n.kind == NodeKind::Symbol && n.text == "-") hyphen = true;
        for (const auto& c : n.children) if (c) walk(*c);
    };
    walk(*parsed.root);
    CHECK(minus);
    CHECK_FALSE(hyphen);
    // Inside \text the hyphen is a hyphen.
    MathParser pt(R"(\text{co-ax})");
    ParseOutput text = pt.parse();
    REQUIRE(text.root != nullptr);
    bool textHyphen = false;
    std::function<void(const MathNode&)> walk2 = [&](const MathNode& n) {
        if (n.kind == NodeKind::Text && n.text.find('-') != std::string::npos) textHyphen = true;
        for (const auto& c : n.children) if (c) walk2(*c);
    };
    walk2(*text.root);
    CHECK(textHyphen);
}
