// Spec 19 §3 "Results" — the counts histogram (sorted by count or by bitstring, little-endian
// labels per the README convention), the shot-memory table with virtual scrolling, the expectation
// values table and the export buttons.
#include "UI/Format.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <imgui.h>

namespace qlab::ui {
namespace {

class ResultsPanel final : public BasicPanel {
public:
    ResultsPanel() : BasicPanel(PanelId::Results, "results", "panels.results", "□", Workspace::Program) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["sort_by_count"] = sortByCount_;
        j["tab"] = tab_;
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (!j.is_object()) return;
        if (const auto it = j.find("sort_by_count"); it != j.end() && it->is_boolean()) sortByCount_ = it->get<bool>();
        if (const auto it = j.find("tab"); it != j.end() && it->is_number_integer()) tab_ = it->get<int>();
    }

private:
    void drawCounts(UiContext& ctx, const runtime::RunResult& r);
    void drawMemory(UiContext& ctx, const runtime::RunResult& r);
    void drawExpectations(UiContext& ctx, const runtime::RunResult& r);

    bool sortByCount_ = true;
    int tab_ = 0;
};

void ResultsPanel::drawCounts(UiContext& ctx, const runtime::RunResult& r) {
    auto rows = r.counts.all();                       // sorted by label
    if (sortByCount_)
        std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (rows.empty()) {
        widgets::text(ctx, Token::TextSecondary, "No shots recorded.");
        return;
    }
    widgets::checkbox(ctx, "Sort by count", &sortByCount_, "Sort counts");
    ImGui::SameLine();
    widgets::fidelityBadge(ctx, r.counts.cls);
    const std::uint64_t total = std::max<std::uint64_t>(1, r.counts.total());
    const std::uint64_t peak = std::max_element(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
                                   return a.second < b.second;
                               })->second;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    // Three columns: the ket, the bar, the numbers. Every label is the same number of bits and the
    // numbers are widest for `peak`, so both outer columns can be measured once and the bar takes
    // what is left — otherwise a wide register or a long confidence interval runs off the panel.
    const float gap = ctx.ui(8.0f);
    const float ketCol = std::max(ctx.ui(60.0f), widgets::textWidth(ctx, FontRole::Code, "|" + rows.front().first + "⟩") + gap);
    const std::string widest = format::integer(peak) + "   100.00 %  [100.0 %, 100.0 %]";
    const float numCol = widgets::textWidth(ctx, FontRole::Body, widest) + gap;
    const float barMax = std::max(ctx.ui(40.0f), ImGui::GetContentRegionAvail().x - ketCol - numCol - gap);
    ImGuiListClipper clipper;                          // spec 19 §5.7: virtual scrolling
    clipper.Begin(static_cast<int>(rows.size()), rowHeight);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& [label, count] = rows[static_cast<std::size_t>(i)];
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float w = barMax * static_cast<float>(count) / static_cast<float>(peak);
            dl->AddRectFilled(ImVec2(at.x + ketCol, at.y + 2.0f), ImVec2(at.x + ketCol + w, at.y + rowHeight - 4.0f),
                              widgets::u32(ctx.th()[Token::Accent]), ctx.metrics_px().radiusSm);
            {
                FontScope f(*ctx.fonts, FontRole::Code);
                widgets::text(ctx, Token::TextPrimary, "|" + label + "⟩");
            }
            // Screen coordinates, not SameLine's offset-from-window-start: the latter ignores the
            // window padding that `at` already carries, and the peak row's bar would run into the text.
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(at.x + ketCol + barMax + gap, ImGui::GetCursorScreenPos().y));
            const data::Interval ci = r.counts.interval(label);
            widgets::text(ctx, Token::TextSecondary,
                          format::integer(count) + "   " + format::percent(static_cast<double>(count) / static_cast<double>(total)) +
                              "  [" + format::percent(ci.lo, 1) + ", " + format::percent(ci.hi, 1) + "]");
        }
    }
    clipper.End();
}

void ResultsPanel::drawMemory(UiContext& ctx, const runtime::RunResult& r) {
    if (r.memory.empty()) {
        widgets::text(ctx, Token::TextSecondary, "Per-shot memory was not kept for this run.");
        return;
    }
    if (!ImGui::BeginTable("##memory", 3, widgets::tableFlags(false))) return;
    ImGui::TableSetupColumn("shot", ImGuiTableColumnFlags_WidthStretch, 0.15f);
    ImGui::TableSetupColumn("bits", ImGuiTableColumnFlags_WidthStretch, 0.55f);
    ImGui::TableSetupColumn("outputs", ImGuiTableColumnFlags_WidthStretch, 0.30f);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;                          // spec 19 §5.7
    clipper.Begin(static_cast<int>(r.memory.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const runtime::ShotRecord& shot = r.memory[static_cast<std::size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            widgets::text(ctx, Token::TextSecondary, format::integer(static_cast<std::uint64_t>(i)));
            ImGui::TableNextColumn();
            FontScope f(*ctx.fonts, FontRole::Code);
            widgets::text(ctx, Token::TextPrimary, r.layout.key(shot.bits));
            ImGui::TableNextColumn();
            std::string outputs;
            for (double v : shot.outputs) outputs += (outputs.empty() ? "" : ", ") + format::number(v);
            widgets::text(ctx, Token::TextSecondary, outputs);
        }
    }
    clipper.End();
    ImGui::EndTable();
}

void ResultsPanel::drawExpectations(UiContext& ctx, const runtime::RunResult& r) {
    if (r.expectations.empty()) {
        widgets::text(ctx, Token::TextSecondary, "No expectation values for this run.");
        return;
    }
    if (!ImGui::BeginTable("##expect", 4, widgets::tableFlags(false))) return;
    ImGui::TableSetupColumn("observable", ImGuiTableColumnFlags_WidthStretch, 0.30f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.30f);
    ImGui::TableSetupColumn("class", ImGuiTableColumnFlags_WidthStretch, 0.20f);
    ImGui::TableSetupColumn("exact", ImGuiTableColumnFlags_WidthStretch, 0.20f);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    for (const runtime::Expectation& e : r.expectations) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextSecondary, e.observable);
        ImGui::TableNextColumn();
        {
            FontScope f(*ctx.fonts, FontRole::Readout);
            widgets::text(ctx, Token::TextPrimary, format::withSigma(e.value, e.stderr_, {}));
        }
        ImGui::TableNextColumn();
        widgets::fidelityBadge(ctx, e.cls);
        ImGui::TableNextColumn();
        // The exact value comes from the final state: Simulator-only (spec 00 §6).
        if (e.exact && !ctx.physicalLab) {
            widgets::text(ctx, ctx.th()[Token::SimOnly], format::number(*e.exact));
            ImGui::SameLine();
            widgets::simOnlyBadge(ctx);
        } else {
            widgets::text(ctx, Token::TextDisabled, "—");
        }
    }
    ImGui::EndTable();
}

void ResultsPanel::draw(UiContext& ctx) {
    if (ctx.result == nullptr) {
        widgets::placeholder(ctx, "Run a program to see its results.");
        return;
    }
    const runtime::RunResult& r = *ctx.result;
    widgets::labelled(ctx, "Device", r.device);
    ImGui::SameLine();
    widgets::labelled(ctx, "Backend", r.backendReason.empty() ? "—" : r.backendReason);
    ImGui::SameLine();
    widgets::labelled(ctx, "Shots", format::integer(r.shotsCompleted));
    ImGui::SameLine();
    widgets::labelled(ctx, "Seed", format::integer(r.seed));
    ImGui::SameLine();
    if (widgets::secondaryButton(ctx, "Export") && ctx.cmd.exportResults) ctx.cmd.exportResults();
    if (r.partial) {
        ImGui::SameLine();
        widgets::badge(ctx, "partial", ctx.th()[Token::Warn]);
    }
    ImGui::Separator();

    if (ImGui::BeginTabBar("##results")) {
        if (ImGui::BeginTabItem("Counts")) {
            tab_ = 0;
            drawCounts(ctx, r);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Shot memory")) {
            tab_ = 1;
            drawMemory(ctx, r);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Expectations")) {
            tab_ = 2;
            drawExpectations(ctx, r);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

} // namespace

PanelPtr makeResultsPanel() { return std::make_unique<ResultsPanel>(); }

} // namespace qlab::ui
