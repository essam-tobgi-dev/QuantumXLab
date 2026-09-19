// Spec 23 §10 — the self-contained HTML run report and the pieces it is built from.
#include "Data/Fidelity.hpp"
#include "ReportTestUtil.hpp"

using namespace qlab;
using namespace qlab::report;

namespace {

std::vector<std::uint8_t> smallPng() {
    rtest::Sandbox box("png_source");
    Image img(8, 8, {10, 20, 30, 255});
    img.fillRect(2, 2, 4, 4, {200, 100, 50, 255});
    REQUIRE(writePng(box / "p.png", img).has_value());
    return rtest::readBytes(box / "p.png");
}

RunReportInput fullInput(const rtest::Fixture& f) {
    RunReportInput in;
    in.title = "Bell pair <sc_fixed_5> & friends";
    in.project = "report tests";
    in.run = &f.result;
    in.compiled = f.compiled;
    in.source = f.source;
    in.circuit.mime = "image/svg+xml";
    in.circuit.svg = "<svg class=\"fig\" viewBox=\"0 0 10 10\"><line x1=\"0\" y1=\"5\" x2=\"10\" "
                     "y2=\"5\"/></svg>";
    in.circuit.caption = "compiled circuit";

    ReportImage plot;
    plot.caption = "readout IQ";
    plot.bytes = smallPng();
    in.plots.push_back(plot);

    StateView view;
    view.name = "Bloch";
    view.cls = data::FidelityClass::Numerical;
    view.simulatorOnly = true;
    view.image = plot;
    view.note = "qubit 0 after the entangler";
    in.states.push_back(view);

    ReportEquation eq;
    eq.caption = "depolarizing probability";
    eq.latex = "p = \\frac{d\\,r}{d - 1}";
    in.equations.push_back(eq);
    ReportEquation rendered;
    rendered.caption = "pre-rendered";
    rendered.image = plot;
    in.equations.push_back(rendered);

    RecipeFigure recipe;
    recipe.name = "T1";
    recipe.cls = data::FidelityClass::Statistical;
    recipe.trace.name = "delay";
    recipe.trace.xUnit = "us";
    recipe.trace.yUnit = "";
    recipe.trace.x = {0.0, 10.0, 20.0, 40.0};
    recipe.trace.y = {1.0, 0.89, 0.79, 0.62};
    recipe.trace.sigma = std::vector<double>{0.01, 0.01, 0.01, 0.01};
    recipe.values = {{"T1", "87.3 us ± 1.2"}, {"chi2/dof", "1.04"}};
    in.recipes.push_back(recipe);

    in.theory.push_back({"T04 §3 — decoherence channels", "T04.html#3"});
    in.extra.push_back({"Notes", "<p>run kept for the calibration log</p>"});
    return in;
}

} // namespace

TEST_CASE("html: escaping, base64 and data URIs") {
    CHECK(htmlEscape("<a href=\"x\">'&'</a>") ==
          "&lt;a href=&quot;x&quot;&gt;&#39;&amp;&#39;&lt;/a&gt;");
    // RFC 4648 test vectors.
    auto b64 = [](std::string_view s) {
        return base64(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()),
                                                    s.size()));
    };
    CHECK(b64("") == "");
    CHECK(b64("f") == "Zg==");
    CHECK(b64("fo") == "Zm8=");
    CHECK(b64("foo") == "Zm9v");
    CHECK(b64("foob") == "Zm9vYg==");
    CHECK(b64("fooba") == "Zm9vYmE=");
    CHECK(b64("foobar") == "Zm9vYmFy");
    const std::uint8_t png[] = {0x89, 'P', 'N', 'G'};
    CHECK(dataUri("image/png", png) == "data:image/png;base64,iVBORw==");
    CHECK(dataUri("image/png", {}).empty());
}

TEST_CASE("html: the placeholder substitution is the whole templating engine (spec 23 §10)") {
    Placeholders values{{"a", "1"}, {"b", "<b>"}};
    CHECK(substitute("x{{a}}y{{ b }}z", values) == "x1y<b>z");
    CHECK(substitute("no placeholders", values) == "no placeholders");
    CHECK(substitute("{{a}}{{a}}", values) == "11");
    // An unterminated placeholder is copied through, never truncated.
    CHECK(substitute("tail {{a", values) == "tail {{a");
    std::vector<std::string> missing;
    CHECK(substitute("{{a}}{{nope}}", values, &missing) == "1");
    REQUIRE(missing.size() == 1u);
    CHECK(missing[0] == "nope");
}

TEST_CASE("html: OpenQASM is highlighted with inline CSS classes only") {
    const std::string html = highlightQasm("// a comment\nqubit[2] q;\nrz(1.5) $3;  // physical\n");
    INFO(html);
    CHECK(html.find("<span class=\"c\">// a comment</span>") != std::string::npos);
    CHECK(html.find("<span class=\"k\">qubit</span>") != std::string::npos);
    CHECK(html.find("<span class=\"n\">2</span>") != std::string::npos);
    CHECK(html.find("<span class=\"g\">$3</span>") != std::string::npos);
    CHECK(html.find("<style") == std::string::npos);
    // The text is escaped before it is wrapped.
    CHECK(highlightQasm("a < b").find("&lt;") != std::string::npos);
}

TEST_CASE("html: the histogram figure is inline SVG with one bar per outcome") {
    data::Histogram h(2);
    h.add("00", 60);
    h.add("11", 40);
    const std::string svg = histogramSvg(h);
    INFO(svg);
    CHECK(svg.starts_with("<svg class=\"fig\""));
    CHECK(svg.find("<rect class=\"bar\"") != std::string::npos);
    CHECK(svg.find(">00 : 60 (0.6)</title>") != std::string::npos);
    CHECK(svg.find(">11 : 40 (0.4)</title>") != std::string::npos);
    CHECK(svg.ends_with("</svg>"));
    CHECK(isSelfContained(svg));
    CHECK(histogramSvg(data::Histogram(2)).find("no counts") != std::string::npos);
}

TEST_CASE("report: every section of spec 23 §10 is present, in order") {
    const rtest::Fixture& f = rtest::bell();
    const RunReportInput in = fullInput(f);
    auto html = renderRunReport(in);
    INFO((html ? std::string() : html.error().format()));
    REQUIRE(html.has_value());

    // §10 fixes the order: provenance, program, circuit, resources, estimate, results, states,
    // recipes, theory.
    static constexpr std::string_view kSections[] = {
        "<h2>Provenance</h2>",        "<h2>Program</h2>",
        "<h2>Compiled circuit</h2>",  "<h2>Resources</h2>",
        "<h2>Hardware estimate</h2>", "<h2>Equations</h2>",
        "<h2>Results</h2>",           "<h2>Plots</h2>",
        "<h2>State views</h2>",       "<h2>Recipes</h2>",
        "<h2>Diagnostics</h2>",       "<h2>Theory references</h2>"};
    std::size_t previous = 0;
    for (std::string_view section : kSections) {
        const std::size_t at = html->find(section);
        INFO(section);
        REQUIRE(at != std::string::npos);
        CHECK(at > previous);
        previous = at;
    }
    CHECK(html->find("<h2>Notes</h2>") > previous); // caller sections come last

    // Provenance: project, device, calibration, seed, hashes, application version.
    CHECK(html->find("report tests") != std::string::npos);
    CHECK(html->find("sc_fixed_5") != std::string::npos);
    CHECK(html->find(f.result.calibrationTimestamp) != std::string::npos);
    CHECK(html->find("20250916") != std::string::npos);
    CHECK(html->find(hashHex(f.result.programHash)) != std::string::npos);
    CHECK(html->find(std::string(core::version())) != std::string::npos);

    // Program: source and compiled QASM, highlighted; the title is escaped.
    CHECK(html->find("<span class=\"k\">qubit</span>") != std::string::npos);
    // `pragma` is a highlighted keyword, so only the pragma's name survives as literal text.
    CHECK(html->find("<span class=\"k\">pragma</span> qlab.mapping") != std::string::npos);
    CHECK(html->find("qlab.estimate wall_s=") != std::string::npos);
    CHECK(html->find("Bell pair &lt;sc_fixed_5&gt; &amp; friends") != std::string::npos);

    // Resources count the qubits the program uses, not the width of the physical circuit.
    CHECK(html->find("<tr><td>Qubits</td><td>" + std::to_string(f.result.qubits.size()) +
                     "</td></tr>") != std::string::npos);
    CHECK(html->find("<tr><td>Two-qubit gates</td><td>" +
                     std::to_string(f.result.metrics.twoQubitCount) + "</td></tr>") !=
          std::string::npos);

    // Results: the histogram, the table and the fidelity badges of spec 00 §5.
    CHECK(html->find("<rect class=\"bar\"") != std::string::npos);
    CHECK(html->find("<th>Bitstring</th>") != std::string::npos);
    CHECK(html->find("<th>68 % interval</th>") != std::string::npos);
    CHECK(html->find("class=\"badge statistical\"") != std::string::npos);

    // Estimate: the assumption list is rendered verbatim (T12 §9).
    REQUIRE_FALSE(f.result.estimate.assumptions.empty());
    for (const auto& key : f.result.estimate.assumptions) {
        const std::string text = std::string(runtime::assumptionText(key));
        REQUIRE_FALSE(text.empty());
        INFO(key << ": " << text);
        CHECK(html->find(htmlEscape(text)) != std::string::npos);
    }

    // States and recipes carry their badges; a Simulator-only view says so.
    CHECK(html->find("Simulator-only</span>") != std::string::npos);
    CHECK(html->find("class=\"badge numerical\"") != std::string::npos);
    CHECK(html->find("87.3 us ± 1.2") != std::string::npos);
    CHECK(html->find("<polyline class=\"line\"") != std::string::npos);
    CHECK(html->find("<polygon class=\"band\"") != std::string::npos);

    // Equations: pre-rendered image or LaTeX text.
    CHECK(html->find("p = \\frac{d\\,r}{d - 1}") != std::string::npos);
    CHECK(html->find("T04 §3 — decoherence channels") != std::string::npos);
    CHECK(html->find("href=\"T04.html#3\"") != std::string::npos);
}

TEST_CASE("report: the file is self-contained (spec 23 §10)") {
    const rtest::Fixture& f = rtest::bell();
    rtest::Sandbox box("report_html");
    const RunReportInput in = fullInput(f);
    REQUIRE(writeRunReport(box / "report.html", in).has_value());
    const std::string html = rtest::readFile(box / "report.html");

    std::vector<std::string> offenders;
    const bool selfContained = isSelfContained(html, &offenders);
    INFO((offenders.empty() ? std::string() : offenders.front()));
    CHECK(selfContained);
    CHECK(offenders.empty());
    CHECK(html.find("http://") == std::string::npos);
    CHECK(html.find("https://") == std::string::npos);
    CHECK(html.find("<script") == std::string::npos);
    CHECK(html.find("@import") == std::string::npos);
    // The images that are in it are embedded, not linked.
    CHECK(html.find("<img alt=\"readout IQ\" src=\"data:image/png;base64,iVBORw0KGgo") !=
          std::string::npos);
    CHECK(html.find("<style>") != std::string::npos);
    CHECK(html.starts_with("<!DOCTYPE html>"));
    CHECK(html.find("</html>") != std::string::npos);

    // The guard catches a section that would reach outside the file.
    RunReportInput leaky = in;
    leaky.extra.push_back({"Bad", "<img src=\"https://example.com/plot.png\">"});
    auto refused = renderRunReport(leaky);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("self-contained") != std::string::npos);
}

TEST_CASE("report: a minimal input still produces every section") {
    RunReportInput in;
    in.title = "empty";
    auto html = renderRunReport(in);
    REQUIRE(html.has_value());
    CHECK(html->find("<h2>Results</h2>") != std::string::npos);
    CHECK(html->find("no run was supplied") != std::string::npos);
    CHECK(html->find("no state view was selected") != std::string::npos);
    CHECK(isSelfContained(*html));

    // A missing template is reported with the path it looked for.
    RunReportInput missing = in;
    missing.templatePath = "/nonexistent/run.html.tmpl";
    auto bad = renderRunReport(missing);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == ErrorCode::Io);
    CHECK(bad.error().format().find("run.html.tmpl") != std::string::npos);
}
