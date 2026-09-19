// Spec 23 §10 — assembling the report: load the template, fill every `{{placeholder}}`, check that
// the result references nothing outside itself.
#include "Core/Paths.hpp"
#include "Core/Version.hpp"
#include "Report/HtmlReport.hpp"

#include <format>

namespace qlab::report {
namespace {

std::string diagnosticsHtml(const RunReportInput& in) {
    if (!in.run || in.run->diagnostics.empty()) {
        if (!in.compiled || in.compiled->diagnostics.empty())
            return "<p class=\"muted\">none</p>";
    }
    std::vector<std::vector<std::string>> rows;
    auto add = [&rows](const std::vector<lang::Diagnostic>& ds, std::string_view origin) {
        for (const auto& d : ds) {
            const char* sev = d.severity == lang::Severity::Error     ? "error"
                              : d.severity == lang::Severity::Warning ? "warning"
                                                                      : "info";
            rows.push_back(
                {std::string(origin), d.id().empty() ? "-" : d.id(), sev, d.error.message});
        }
    };
    if (in.compiled)
        add(in.compiled->diagnostics, "compile");
    if (in.run)
        add(in.run->diagnostics, "run");
    if (rows.empty())
        return "<p class=\"muted\">none</p>";
    static const std::string headers[] = {"Stage", "Id", "Severity", "Message"};
    return tableHtml(headers, rows);
}

std::string plotsHtml(const RunReportInput& in) {
    std::string html;
    for (const auto& p : in.plots)
        html += imageHtml(p);
    return html.empty() ? "<p class=\"muted\">no plot was exported</p>" : html;
}

std::string circuitHtml(const RunReportInput& in) {
    if (!in.circuit.empty())
        return imageHtml(in.circuit);
    return "<p class=\"muted\">no circuit diagram was supplied</p>";
}

std::string extraHtml(const RunReportInput& in) {
    std::string html;
    for (const auto& s : in.extra)
        html += "<section><h2>" + htmlEscape(s.title) + "</h2>" + s.html + "</section>\n";
    return html;
}

std::string subtitle(const RunReportInput& in) {
    std::string s;
    if (!in.project.empty())
        s = in.project;
    if (in.run) {
        if (!s.empty())
            s += " — ";
        s += in.run->device.empty() ? std::string("device-independent") : in.run->device;
        s += std::format(", {} shots on {}", in.run->options.shots, backendName(in.run->backend));
    }
    return htmlEscape(s);
}

} // namespace

Result<std::string> loadReportTemplate(const std::filesystem::path& templatePath) {
    const std::filesystem::path p =
        templatePath.empty() ? core::assetDir() / "Report" / "run.html.tmpl" : templatePath;
    auto text = core::readTextFile(p);
    if (!text) {
        Error e = text.error();
        e.notes.push_back(
            "the run report template of spec 23 §10 lives in Assets/Report/run.html.tmpl");
        return std::unexpected(std::move(e));
    }
    return text;
}

Result<std::string> renderRunReport(const RunReportInput& in) {
    QXL_TRY_ASSIGN(const std::string tmpl, loadReportTemplate(in.templatePath));
    Placeholders values;
    values["title"] = htmlEscape(in.title);
    values["subtitle"] = subtitle(in);
    values["provenance"] = provenanceHtml(in);
    values["program"] = programHtml(in);
    values["circuit"] = circuitHtml(in);
    values["resources"] = resourcesHtml(in);
    values["estimate"] =
        in.run ? estimateHtml(in.run->estimate) : std::string("<p class=\"muted\">no estimate</p>");
    values["equations"] = equationsHtml(in);
    values["results"] = resultsHtml(in);
    values["plots"] = plotsHtml(in);
    values["states"] = statesHtml(in);
    values["recipes"] = recipesHtml(in);
    values["diagnostics"] = diagnosticsHtml(in);
    values["theory"] = theoryHtml(in);
    values["extra"] = extraHtml(in);
    values["app"] = htmlEscape(core::version());
    values["generated"] = htmlEscape(core::isoNow());

    std::vector<std::string> missing;
    std::string html = substitute(tmpl, values, &missing);
    if (!missing.empty()) {
        std::string list;
        for (const auto& m : missing)
            list += (list.empty() ? "" : ", ") + m;
        return fail(ErrorCode::InvalidArgument,
                    "report template has unknown placeholders: " + list);
    }
    std::vector<std::string> offenders;
    if (!isSelfContained(html, &offenders)) {
        std::string list;
        for (const auto& o : offenders)
            list += (list.empty() ? "" : " | ") + o;
        return fail(ErrorCode::InvalidArgument,
                    "report would not be self-contained (spec 23 §10): " + list);
    }
    return html;
}

Status writeRunReport(const std::filesystem::path& path, const RunReportInput& in) {
    QXL_TRY_ASSIGN(const std::string html, renderRunReport(in));
    return core::writeTextFileAtomic(path, html);
}

} // namespace qlab::report
