#pragma once
// Spec 23 §10 — the self-contained HTML run report. Inline CSS, inline SVG figures, caller-supplied
// images embedded as base64 data URIs, equations as pre-rendered SVG/PNG (spec 20 §2) or as LaTeX
// text, no scripts and no external references.
//
// The template is `Assets/Report/run.html.tmpl` with `{{placeholders}}`; `report::substitute` is
// the whole templating engine (§10: "no templating library").
#include "Compiler/Pass.hpp"
#include "Data/Fidelity.hpp"
#include "Data/Series.hpp"
#include "Report/Html.hpp"
#include "Runtime/Run.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace qlab::report {

// An image the caller already rendered (a circuit diagram, an ImPlot capture, an equation).
struct ReportImage {
    std::string caption;
    std::string mime = "image/png";      // "image/png" or "image/svg+xml"
    std::vector<std::uint8_t> bytes;     // embedded as a data URI
    std::string svg;                     // inline SVG text; used when `bytes` is empty
    bool empty() const { return bytes.empty() && svg.empty(); }
};

struct ReportEquation {
    std::string caption;
    std::string latex;                   // shown as text when no pre-rendered image is given
    ReportImage image;
};

// A state view the user selected, with its fidelity / observability badge (spec 23 §10).
struct StateView {
    std::string name;
    data::FidelityClass cls = data::FidelityClass::Exact;
    bool simulatorOnly = true;
    ReportImage image;
    std::string note;
};

// A calibration recipe's fit plot and its result table (spec 22 §6).
struct RecipeFigure {
    std::string name;
    ReportImage plot;                                 // pre-rendered; else `trace` is drawn inline
    data::Trace2D trace;
    data::FidelityClass cls = data::FidelityClass::Statistical;
    std::vector<std::pair<std::string, std::string>> values;   // "T1", "87.3 us ± 1.2"
};

// A link into the theory corpus; `anchor` is a document-relative URL (`T04.html#4-2`).
struct TheoryLink {
    std::string title;
    std::string anchor;
};

struct ReportSection {
    std::string title;
    std::string html;
};

struct RunReportInput {
    std::string title = "QuantumXLab run report";
    std::string project;
    const runtime::RunResult* run = nullptr;
    const compiler::CompiledProgram* compiled = nullptr;
    std::string source;                  // program text (syntax-highlighted into the report)
    std::string compiledQasmText;        // "" = produced from `compiled` by `compiledQasm`
    ReportImage circuit;                 // circuit diagram, SVG preferred (spec 23 §8)
    std::vector<ReportImage> plots;
    std::vector<StateView> states;
    std::vector<ReportEquation> equations;
    std::vector<RecipeFigure> recipes;
    std::vector<TheoryLink> theory;
    std::vector<ReportSection> extra;
    std::filesystem::path templatePath;  // "" = Assets/Report/run.html.tmpl
};

// The template of §10 (from `templatePath`, or the shipped asset).
Result<std::string> loadReportTemplate(const std::filesystem::path& templatePath = {});
Result<std::string> renderRunReport(const RunReportInput& in);
Status writeRunReport(const std::filesystem::path& path, const RunReportInput& in);

// The individual section bodies, exposed so the App can reuse them in a panel.
std::string provenanceHtml(const RunReportInput& in);
std::string programHtml(const RunReportInput& in);
std::string resourcesHtml(const RunReportInput& in);
std::string estimateHtml(const runtime::Estimate& e);
std::string resultsHtml(const RunReportInput& in);
std::string statesHtml(const RunReportInput& in);
std::string recipesHtml(const RunReportInput& in);
std::string equationsHtml(const RunReportInput& in);
std::string theoryHtml(const RunReportInput& in);
std::string imageHtml(const ReportImage& img);

} // namespace qlab::report
