// Spec 23 §10 — the body of each report section, in the order the spec fixes.
#include "Core/Version.hpp"
#include "Data/Fidelity.hpp"
#include "Data/Writers.hpp"
#include "Report/HtmlReport.hpp"
#include "Report/ProgramExport.hpp"

#include <format>

namespace qlab::report {
namespace {

using Row = std::vector<std::string>;

std::string num(double v, int digits = 6) { return std::format("{:.{}g}", v, digits); }

std::string seconds(double s) {
    if (s == 0.0) return "0 s";
    const double a = std::abs(s);
    if (a < 1e-9) return std::format("{:.3g} ps", s * 1e12);
    if (a < 1e-6) return std::format("{:.3g} ns", s * 1e9);
    if (a < 1e-3) return std::format("{:.3g} us", s * 1e6);
    if (a < 1.0) return std::format("{:.3g} ms", s * 1e3);
    return std::format("{:.4g} s", s);
}

std::string bytes(double b) {
    static constexpr const char* kUnit[] = {"B", "kB", "MB", "GB", "TB", "PB"};
    int u = 0;
    while (b >= 1000.0 && u < 5) {
        b /= 1000.0;
        ++u;
    }
    return std::format("{:.3g} {}", b, kUnit[u]);
}

std::string kv(std::span<const std::pair<std::string, std::string>> rows) {
    std::vector<Row> body;
    for (const auto& [k, v] : rows) body.push_back({k, v});
    static const std::string headers[] = {"Field", "Value"};
    return tableHtml(headers, body);
}

std::string orNone(std::string html, std::string_view none = "none") {
    return html.empty() ? "<p class=\"muted\">" + std::string(none) + "</p>" : html;
}

} // namespace

std::string imageHtml(const ReportImage& img) {
    if (img.empty()) return {};
    std::string body;
    if (!img.svg.empty())
        body = img.svg;   // already inline SVG
    else
        body = "<img alt=\"" + htmlEscape(img.caption) + "\" src=\"" + dataUri(img.mime, img.bytes) + "\">";
    std::string out = "<figure>" + body;
    if (!img.caption.empty()) out += "<figcaption>" + htmlEscape(img.caption) + "</figcaption>";
    out += "</figure>";
    return out;
}

std::string provenanceHtml(const RunReportInput& in) {
    std::vector<std::pair<std::string, std::string>> rows;
    if (!in.project.empty()) rows.emplace_back("Project", in.project);
    if (in.run) {
        const RunIdentity id = RunIdentity::of(*in.run);
        rows.emplace_back("Device", id.device.empty() ? "(device-independent)" : id.device);
        rows.emplace_back("Calibration", id.calibrationTime.empty() ? "-" : id.calibrationTime);
        rows.emplace_back("Backend", id.backend + (in.run->backendReason.empty() ? "" : " (" + in.run->backendReason + ")"));
        rows.emplace_back("Shots", std::to_string(id.shots) +
                                       (in.run->shotsCompleted != id.shots
                                            ? " (" + std::to_string(in.run->shotsCompleted) + " completed)"
                                            : ""));
        rows.emplace_back("Seed", std::to_string(id.seed));
        rows.emplace_back("Program hash", id.programHash);
        rows.emplace_back("Device hash", id.deviceHash);
        rows.emplace_back("Run wall time", seconds(std::chrono::duration<double>(in.run->wallTime).count()));
    } else if (in.compiled) {
        rows.emplace_back("Device", in.compiled->deviceId.empty() ? "(device-independent)" : in.compiled->deviceId);
        rows.emplace_back("Calibration", in.compiled->calibrationTimestamp);
        rows.emplace_back("Program hash", hashHex(in.compiled->programHash));
    }
    rows.emplace_back("Application", std::string(core::version()));
    rows.emplace_back("Generated", core::isoNow());
    std::string html = kv(rows);
    if (in.run) html += "<p>" + fidelityBadge(in.run->backendClass, "backend") + "</p>";
    return html;
}

std::string programHtml(const RunReportInput& in) {
    std::string html;
    if (!in.source.empty())
        html += "<h3>Source</h3><pre><code>" + highlightQasm(in.source) + "</code></pre>";
    std::string compiledText = in.compiledQasmText;
    if (compiledText.empty() && in.compiled) {
        ProgramExportOptions opts;
        if (in.run) opts.estimate = &in.run->estimate;
        if (auto text = compiledQasm(*in.compiled, opts)) compiledText = *text;
    }
    if (!compiledText.empty())
        html += "<h3>Compiled (OpenQASM 3)</h3><pre><code>" + highlightQasm(compiledText) + "</code></pre>";
    return orNone(html, "no program was supplied");
}

std::string resourcesHtml(const RunReportInput& in) {
    const compiler::PassMetrics* m = nullptr;
    if (in.run) m = &in.run->metrics;
    else if (in.compiled) m = &in.compiled->metrics;
    if (!m) return orNone({}, "no compiled program");

    std::vector<std::pair<std::string, std::string>> rows;
    const ir::Circuit* circuit = in.run && in.run->circuit ? in.run->circuit.get()
                                 : in.compiled                ? &in.compiled->circuit
                                                              : nullptr;
    // The qubits the program uses, not the width of the physical circuit (spec 15 §8 / 23 §6).
    if (in.run && !in.run->qubits.empty())
        rows.emplace_back("Qubits", std::to_string(in.run->qubits.size()));
    else if (circuit)
        rows.emplace_back("Qubits", std::to_string(circuit->qubitCount()));
    rows.emplace_back("Depth", std::to_string(m->depth));
    rows.emplace_back("Gates", std::to_string(m->gateCount));
    rows.emplace_back("Two-qubit gates", std::to_string(m->twoQubitCount));
    rows.emplace_back("T gates", std::to_string(m->tCount));
    rows.emplace_back("Swaps inserted", std::to_string(m->swapCount));
    if (m->estimatedDuration.value > 0)
        rows.emplace_back("Circuit duration", seconds(static_cast<double>(m->estimatedDuration.value) * 1e-12));
    if (in.run) rows.emplace_back("Classical bits", std::to_string(in.run->layout.bits));
    std::string html = kv(rows);

    if (circuit) {
        std::vector<Row> byName;
        for (const auto& [name, n] : circuit->gateCounts()) byName.push_back({name, std::to_string(n)});
        if (!byName.empty()) {
            static const std::string headers[] = {"Operation", "Count"};
            html += "<h3>Operations</h3>" + tableHtml(headers, byName);
        }
    }
    if (in.compiled && !in.compiled->initialLayout.empty()) {
        html += "<h3>Layout</h3><p><code>" + htmlEscape(in.compiled->initialLayout.text()) + "</code>";
        if (!in.compiled->finalLayout.empty() && !(in.compiled->finalLayout == in.compiled->initialLayout))
            html += " &rarr; <code>" + htmlEscape(in.compiled->finalLayout.text()) + "</code>";
        html += "</p>";
    }
    return html;
}

std::string estimateHtml(const runtime::Estimate& e) {
    std::vector<std::pair<std::string, std::string>> rows;
    rows.emplace_back("Device", e.device.empty() ? "-" : e.device);
    rows.emplace_back("Wall time", seconds(e.wallTime.valueS) + " (" + seconds(e.wallTime.minS) + " … " +
                                       seconds(e.wallTime.maxS) + ")");
    rows.emplace_back("Per shot", seconds(e.wallTime.perShotS));
    rows.emplace_back("Fidelity", num(e.fidelity.fast, 4) + " (" + num(e.fidelity.low, 4) + " … " +
                                      num(e.fidelity.high, 4) + ")");
    rows.emplace_back("Gate / idle / readout", num(e.fidelity.gateProduct, 4) + " / " +
                                                   num(e.fidelity.idleProduct, 4) + " / " +
                                                   num(e.fidelity.readoutProduct, 4));
    if (e.fidelity.simulated) {
        rows.emplace_back("Simulated classical fidelity", num(e.fidelity.simulated->classical, 4));
        if (e.fidelity.simulated->state) rows.emplace_back("Simulated state fidelity", num(*e.fidelity.simulated->state, 4));
    }
    rows.emplace_back("State-vector memory", bytes(e.classicalCost.stateVectorBytes));
    rows.emplace_back("Classical simulation", seconds(e.classicalCost.estimatedTimeS));
    std::string html = "<p>" + fidelityBadge(e.cls) + "</p>" + kv(rows);

    if (!e.fidelity.caveats.empty()) {
        html += "<h3>Caveats</h3><ul class=\"assumptions\">";
        for (const auto& c : e.fidelity.caveats) html += "<li>" + htmlEscape(c) + "</li>";
        html += "</ul>";
    }
    if (e.qec) {
        std::vector<std::pair<std::string, std::string>> q;
        q.emplace_back("Code distance", std::to_string(e.qec->distance));
        q.emplace_back("Physical qubits", num(e.qec->physicalQubits, 4));
        q.emplace_back("Factories", std::to_string(e.qec->factories));
        q.emplace_back("Wall time", seconds(e.qec->wallTimeS));
        q.emplace_back("Failure probability", num(e.qec->totalFailureProbability, 3));
        html += "<h3>Fault-tolerant estimate " + fidelityBadge(e.qec->cls) + "</h3>" + kv(q);
        if (!e.qec->assumptions.empty()) {
            html += "<ul class=\"assumptions\">";
            for (const auto& a : e.qec->assumptions) html += "<li>" + htmlEscape(a) + "</li>";
            html += "</ul>";
        }
    }
    if (!e.comparison.empty()) {
        std::vector<Row> rowsCmp;
        for (const auto& c : e.comparison)
            rowsCmp.push_back({c.device, seconds(c.wallTimeS), num(c.fidelityFast, 4)});
        static const std::string headers[] = {"Device", "Wall time", "Fidelity"};
        html += "<h3>Other devices</h3>" + tableHtml(headers, rowsCmp);
    }
    // Spec 23 §10 / T12 §9: the assumption list is rendered verbatim with the estimate.
    html += "<h3>Assumptions</h3>";
    if (e.assumptions.empty()) {
        html += "<p class=\"muted\">none recorded</p>";
    } else {
        html += "<ul class=\"assumptions\">";
        for (const auto& key : e.assumptions) {
            const std::string_view text = runtime::assumptionText(key);
            html += "<li>" + htmlEscape(text.empty() ? key : std::string(text)) + "</li>";
        }
        html += "</ul>";
    }
    return html;
}

std::string resultsHtml(const RunReportInput& in) {
    if (!in.run) return orNone({}, "no run was supplied");
    const runtime::RunResult& r = *in.run;
    std::string html = "<p>" + fidelityBadge(r.counts.cls) + "</p>";
    html += histogramSvg(r.counts);

    std::vector<Row> rows;
    for (const auto& [label, count] : r.counts.topK(32)) {
        const data::Interval ci = r.counts.interval(label);
        rows.push_back({label, std::to_string(count), num(r.counts.probability(label), 5),
                        "[" + num(ci.lo, 4) + ", " + num(ci.hi, 4) + "]"});
    }
    static const std::string headers[] = {"Bitstring", "Count", "Probability", "68 % interval"};
    html += tableHtml(headers, rows);
    if (r.counts.distinct() > rows.size())
        html += std::format("<p class=\"muted\">{} of {} distinct outcomes shown.</p>", rows.size(), r.counts.distinct());

    if (!r.expectations.empty()) {
        std::vector<Row> ex;
        for (const auto& e : r.expectations)
            ex.push_back({e.observable, num(e.value, 5), num(e.stderr_, 3),
                          e.exact ? num(*e.exact, 5) : std::string("-"), std::string(data::fidelityName(e.cls))});
        static const std::string exHeaders[] = {"Observable", "Value", "σ", "Exact", "Class"};
        html += "<h3>Expectation values</h3>" + tableHtml(exHeaders, ex);
    }
    if (r.sweep && !r.sweep->points.empty() && r.sweep->axes.size() == 1) {
        std::vector<double> x, y, sigma;
        for (const auto& p : r.sweep->points) {
            x.push_back(p.coords.empty() ? 0.0 : p.coords.front());
            y.push_back(p.p1);
            sigma.push_back(p.p1Stderr);
        }
        const auto& axis = r.sweep->axes.front();
        html += "<h3>Sweep</h3>" + seriesSvg(x, y, sigma, axis.input + " (" + axis.unit + ")", "P(1)");
    }
    return html;
}

std::string statesHtml(const RunReportInput& in) {
    std::string html;
    for (const auto& s : in.states) {
        html += "<h3>" + htmlEscape(s.name) + " " + fidelityBadge(s.cls);
        if (s.simulatorOnly) html += " <span class=\"simonly\">Simulator-only</span>";
        html += "</h3>";
        if (!s.note.empty()) html += "<p class=\"muted\">" + htmlEscape(s.note) + "</p>";
        html += imageHtml(s.image);
    }
    return orNone(html, "no state view was selected");
}

std::string recipesHtml(const RunReportInput& in) {
    std::string html;
    for (const auto& rec : in.recipes) {
        html += "<h3>" + htmlEscape(rec.name) + " " + fidelityBadge(rec.cls) + "</h3>";
        if (!rec.plot.empty())
            html += imageHtml(rec.plot);
        else if (!rec.trace.x.empty())
            html += seriesSvg(rec.trace.x, rec.trace.y, rec.trace.sigma ? *rec.trace.sigma : std::span<const double>{},
                              rec.trace.name + " (" + rec.trace.xUnit + ")", rec.trace.yUnit);
        if (!rec.values.empty()) html += kv(rec.values);
    }
    return orNone(html, "no recipe was run");
}

std::string equationsHtml(const RunReportInput& in) {
    std::string html;
    for (const auto& eq : in.equations) {
        if (!eq.image.empty()) {
            ReportImage img = eq.image;
            if (img.caption.empty()) img.caption = eq.caption;
            html += imageHtml(img);
        } else if (!eq.latex.empty()) {
            // Spec 23 §10: without a pre-rendered image the equation is kept as LaTeX text.
            html += "<figure><pre class=\"eq\"><code>" + htmlEscape(eq.latex) + "</code></pre>";
            if (!eq.caption.empty()) html += "<figcaption>" + htmlEscape(eq.caption) + "</figcaption>";
            html += "</figure>";
        }
    }
    return orNone(html, "no equation was included");
}

std::string theoryHtml(const RunReportInput& in) {
    if (in.theory.empty()) return orNone({}, "no theory reference");
    std::string html = "<ul>";
    for (const auto& t : in.theory)
        html += "<li><a href=\"" + htmlEscape(t.anchor) + "\">" + htmlEscape(t.title) + "</a></li>";
    html += "</ul>";
    return html;
}

} // namespace qlab::report
