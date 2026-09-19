// Spec 19 §3 "Code Editor" and "Diagnostics" — the OpenQASM 3 / OpenPulse editor panel and the
// diagnostics list beside it. The panel owns the buffer (UI state) and nothing else: diagnostics
// come from the incremental compiler (spec 14 §11, 150 ms debounce) through the context, and a
// click on a diagnostic is routed back to the editor by the Shell's `gotoSource` command.
#include "UI/Editor/CodeEditor.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <imgui.h>

namespace qlab::ui {
namespace {

// The single editor instance both panels talk to. The Code Editor panel owns it; the Diagnostics
// panel reaches it through `ctx.cmd.gotoSource`, which the App wires to `CodeEditorPanel::goTo`.
class CodeEditorPanel final : public BasicPanel {
  public:
    CodeEditorPanel()
        : BasicPanel(PanelId::CodeEditor, "code_editor", "panels.code_editor", "¶",
                     Workspace::Program) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override { return editor_.serialize(); }
    void deserialize(const core::Json& j) override { editor_.deserialize(j); }

  private:
    editor::CodeEditor editor_;
    std::uint64_t submittedRevision_ = 0;
};

void CodeEditorPanel::draw(UiContext& ctx) {
    // Diagnostics published for this buffer (spec 14 §11 / 15 §10). The list is tens of entries,
    // so it is simply re-taken each frame rather than guessed at with a stamp.
    if (ctx.diagnostics != nullptr)
        editor_.model().setDiagnostics(*ctx.diagnostics);

    // The Diagnostics panel and the Examples library jump into the buffer through the context.
    if (!ctx.cmd.gotoSource)
        ctx.cmd.gotoSource = [this](const SourceSpan& span) { editor_.gotoSpan(span); };
    if (!ctx.cmd.openProgram)
        ctx.cmd.openProgram = [this](std::string source, std::string) {
            editor_.model().setSource(std::move(source));
        };

    const Metrics m = ctx.metrics_px();
    widgets::text(ctx, Token::TextSecondary,
                  std::to_string(editor_.model().doc().lineCount()) + " lines");
    ImGui::SameLine(0.0f, m.spacing(3));
    if (editor_.model().errorCount() > 0)
        widgets::badge(ctx, std::to_string(editor_.model().errorCount()) + " errors",
                       ctx.th()[Token::Err]);
    else if (editor_.model().warningCount() > 0)
        widgets::badge(ctx, std::to_string(editor_.model().warningCount()) + " warnings",
                       ctx.th()[Token::Warn]);
    else
        widgets::badge(ctx, "OK", ctx.th()[Token::Ok]);
    ImGui::SameLine(0.0f, m.spacing(3));
    widgets::text(ctx, Token::TextSecondary,
                  "Ln " + std::to_string(editor_.caret().line) + ", Col " +
                      std::to_string(editor_.caret().column));
    ImGui::Separator();

    const bool changed = editor_.draw(ctx);
    // Spec 14 §11: an edit re-submits to the incremental compiler, which debounces 150 ms and
    // cancels the compile in flight.
    if (changed && ctx.incremental != nullptr &&
        editor_.model().doc().revision() != submittedRevision_) {
        submittedRevision_ = editor_.model().doc().revision();
        ctx.incremental->submit(editor_.model().doc().text(), "editor.qasm");
    }
}

// ---------------------------------------------------------------- Diagnostics

class DiagnosticsPanel final : public BasicPanel {
  public:
    DiagnosticsPanel()
        : BasicPanel(PanelId::Diagnostics, "diagnostics", "panels.diagnostics", "⚠",
                     Workspace::Program) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["show_info"] = showInfo_;
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (j.is_object())
            if (const auto it = j.find("show_info"); it != j.end() && it->is_boolean())
                showInfo_ = it->get<bool>();
    }

  private:
    bool showInfo_ = true;
};

void DiagnosticsPanel::draw(UiContext& ctx) {
    if (ctx.diagnostics == nullptr || ctx.diagnostics->empty()) {
        widgets::placeholder(ctx, ctx.text("diagnostics.none"));
        return;
    }
    std::uint32_t errors = 0, warnings = 0;
    for (const lang::Diagnostic& d : *ctx.diagnostics) {
        if (d.severity == lang::Severity::Error)
            ++errors;
        else if (d.severity == lang::Severity::Warning)
            ++warnings;
    }
    widgets::badge(ctx, std::to_string(errors) + " errors", ctx.th()[Token::Err]);
    ImGui::SameLine();
    widgets::badge(ctx, std::to_string(warnings) + " warnings", ctx.th()[Token::Warn]);
    ImGui::SameLine();
    widgets::checkbox(ctx, "Info", &showInfo_);
    ImGui::Separator();

    if (!ImGui::BeginTable("##diags", 4, widgets::tableFlags(false)))
        return;
    ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthStretch, 0.12f);
    ImGui::TableSetupColumn("where", ImGuiTableColumnFlags_WidthStretch, 0.16f);
    ImGui::TableSetupColumn("message", ImGuiTableColumnFlags_WidthStretch, 0.56f);
    ImGui::TableSetupColumn("fix", ImGuiTableColumnFlags_WidthStretch, 0.16f);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    int row = 0;
    for (const lang::Diagnostic& d : *ctx.diagnostics) {
        if (!showInfo_ && d.severity == lang::Severity::Info)
            continue;
        ImGui::PushID(row++);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const Token token = d.severity == lang::Severity::Error     ? Token::Err
                            : d.severity == lang::Severity::Warning ? Token::Warn
                                                                    : Token::Accent;
        widgets::text(ctx, token, d.id().empty() ? "—" : d.id());
        ImGui::TableNextColumn();
        const std::string where = d.error.span ? std::to_string(d.error.span->line) + ":" +
                                                     std::to_string(d.error.span->column)
                                               : std::string("—");
        widgets::text(ctx, Token::TextSecondary, where);
        // Spec 19 §3: clicking a diagnostic jumps to its source span.
        if (ImGui::IsItemClicked() && d.error.span && ctx.cmd.gotoSource)
            ctx.cmd.gotoSource(*d.error.span);
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextPrimary, d.error.message);
        if (ImGui::IsItemClicked() && d.error.span && ctx.cmd.gotoSource)
            ctx.cmd.gotoSource(*d.error.span);
        ImGui::TableNextColumn();
        if (!d.fix.empty())
            widgets::text(ctx, Token::Ok, d.fix);
        else if (!d.error.notes.empty())
            widgets::text(ctx, Token::TextSecondary, d.error.notes.front());
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace

PanelPtr makeCodeEditorPanel() {
    return std::make_unique<CodeEditorPanel>();
}
PanelPtr makeDiagnosticsPanel() {
    return std::make_unique<DiagnosticsPanel>();
}

} // namespace qlab::ui
