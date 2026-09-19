// Spec 19 §3 "Log" — the spdlog ring buffer (`core::recentLog`) with the Log / Perf / Jobs tabs.
// Perf shows the frame budget of spec 18 §11 / 24; Jobs shows what the `core::JobSystem` is doing
// and what the run in flight has completed.
#include "Core/Log.hpp"
#include "Data/Fidelity.hpp"
#include "UI/Format.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <array>
#include <imgui.h>

namespace qlab::ui {
namespace {

Token levelToken(core::LogLevel lvl) {
    switch (lvl) {
    case core::LogLevel::Error:
        return Token::Err;
    case core::LogLevel::Warn:
        return Token::Warn;
    case core::LogLevel::Info:
        return Token::TextPrimary;
    case core::LogLevel::Debug:
    case core::LogLevel::Trace:
        break;
    }
    return Token::TextSecondary;
}

std::string_view levelName(core::LogLevel lvl) {
    switch (lvl) {
    case core::LogLevel::Trace:
        return "trace";
    case core::LogLevel::Debug:
        return "debug";
    case core::LogLevel::Info:
        return "info";
    case core::LogLevel::Warn:
        return "warn";
    case core::LogLevel::Error:
        return "error";
    }
    return "?";
}

class LogPanel final : public BasicPanel {
  public:
    LogPanel() : BasicPanel(PanelId::Log, "log", "panels.log", "·", Workspace::Lab) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["min_level"] = minLevel_;
        j["autoscroll"] = autoScroll_;
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (!j.is_object())
            return;
        if (const auto it = j.find("min_level"); it != j.end() && it->is_number_integer())
            minLevel_ = it->get<int>();
        if (const auto it = j.find("autoscroll"); it != j.end() && it->is_boolean())
            autoScroll_ = it->get<bool>();
    }

  private:
    void drawLog(UiContext& ctx);
    void drawPerf(UiContext& ctx);
    void drawJobs(UiContext& ctx);

    int minLevel_ = 2; // Info
    bool autoScroll_ = true;
    std::array<float, 180> frameMs_{};
    std::size_t frameAt_ = 0;
};

void LogPanel::drawLog(UiContext& ctx) {
    static constexpr std::array<std::string_view, 5> kLevels{"trace", "debug", "info", "warn",
                                                             "error"};
    widgets::combo(ctx, "Level", &minLevel_, kLevels, "Log level");
    ImGui::SameLine();
    widgets::checkbox(ctx, "Auto-scroll", &autoScroll_, "Auto-scroll");
    ImGui::Separator();
    ImGui::BeginChild("##log");
    {
        // The font push must close before EndChild: ImGui balances the stacks per window.
        FontScope f(*ctx.fonts, FontRole::CodeSmall);
        const std::vector<core::LogEntry> entries = core::recentLog(500);
        for (const core::LogEntry& e : entries) {
            if (static_cast<int>(e.lvl) < minLevel_)
                continue;
            widgets::text(ctx, Token::TextDisabled, format::number(e.timeS, 6));
            ImGui::SameLine();
            widgets::text(ctx, Token::TextSecondary, core::catName(e.cat));
            ImGui::SameLine();
            widgets::text(ctx, levelToken(e.lvl), levelName(e.lvl));
            ImGui::SameLine();
            widgets::text(ctx, levelToken(e.lvl), e.text);
        }
        if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void LogPanel::drawPerf(UiContext& ctx) {
    // Spec 18 §11 / 24: the frame budget, the renderer's pass timings and the scene statistics.
    frameMs_[frameAt_] = static_cast<float>(ctx.deltaS * 1000.0);
    frameAt_ = (frameAt_ + 1) % frameMs_.size();
    const float latest = frameMs_[(frameAt_ + frameMs_.size() - 1) % frameMs_.size()];
    widgets::readout(ctx, "Frame", latest * 1e-3,
                     widgets::FieldSpec{.unit = "s", .cls = data::FidelityClass::Numerical});
    ImGui::SameLine();
    widgets::badge(ctx, latest <= 16.7f ? "within budget" : "over budget",
                   ctx.th()[latest <= 16.7f ? Token::Ok : Token::Warn]);
    ImGui::PlotLines("##frames", frameMs_.data(), static_cast<int>(frameMs_.size()),
                     static_cast<int>(frameAt_), nullptr, 0.0f, 33.4f,
                     ImVec2(-1.0f, ctx.ui(60.0f)));
    if (ctx.renderer != nullptr) {
        const gfx::FrameStats& s = ctx.renderer->stats();
        widgets::labelled(ctx, "Draw calls",
                          format::integer(static_cast<std::uint64_t>(s.drawCalls)));
        ImGui::SameLine();
        widgets::labelled(ctx, "Triangles", format::integer(s.triangles));
        widgets::labelled(ctx, "Shadow", format::value(s.shadowMs * 1e-3, "s"));
        ImGui::SameLine();
        widgets::labelled(ctx, "Main", format::value(s.mainMs * 1e-3, "s"));
        ImGui::SameLine();
        widgets::labelled(ctx, "Post", format::value(s.postMs * 1e-3, "s"));
    }
    if (ctx.sceneRenderer != nullptr) {
        const lab::RenderStats& s = ctx.sceneRenderer->stats();
        widgets::labelled(ctx, "Considered", format::integer(s.considered));
        ImGui::SameLine();
        widgets::labelled(ctx, "Frustum culled", format::integer(s.frustumCulled));
        ImGui::SameLine();
        widgets::labelled(ctx, "Instanced batches", format::integer(s.instancedBatches));
    }
    if (ctx.math != nullptr && ctx.math->ready())
        widgets::labelled(ctx, "Math layout cache",
                          format::integer(ctx.math->renderer().cacheSize()));
}

void LogPanel::drawJobs(UiContext& ctx) {
    if (ctx.jobs != nullptr) {
        widgets::labelled(ctx, "Workers", format::integer(ctx.jobs->workerCount()));
        ImGui::SameLine();
        widgets::labelled(ctx, "Pending", format::integer(ctx.jobs->pending()));
    }
    if (ctx.bus != nullptr)
        widgets::labelled(ctx, "Queued events", format::integer(ctx.bus->pending()));
    if (ctx.liveRunner != nullptr) {
        widgets::labelled(ctx, "Live channels", format::integer(ctx.liveRunner->liveChannels()));
        ImGui::SameLine();
        widgets::labelled(ctx, "Acquisitions in flight",
                          format::integer(ctx.liveRunner->inFlight()));
    }
    if (ctx.session == nullptr)
        return;
    widgets::sectionHeader(ctx, "Run history");
    const std::vector<runtime::RunRecord> history = ctx.session->history();
    if (history.empty()) {
        widgets::text(ctx, Token::TextSecondary, "No runs yet.");
        return;
    }
    if (!ImGui::BeginTable("##runs", 5, widgets::tableFlags(false)))
        return;
    ImGui::TableSetupColumn("run");
    ImGui::TableSetupColumn("device");
    ImGui::TableSetupColumn("backend");
    ImGui::TableSetupColumn("shots");
    ImGui::TableSetupColumn("wall time");
    ImGui::TableHeadersRow();
    for (const runtime::RunRecord& r : history) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextSecondary, format::integer(r.id.get()));
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextPrimary, r.device);
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextPrimary, r.backend);
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextPrimary, format::integer(r.shots));
        ImGui::TableNextColumn();
        widgets::text(ctx, r.partial ? Token::Warn : Token::TextPrimary,
                      format::value(static_cast<double>(r.wallTime.count()) * 1e-9, "s"));
    }
    ImGui::EndTable();
}

void LogPanel::draw(UiContext& ctx) {
    if (!ImGui::BeginTabBar("##logtabs"))
        return;
    if (ImGui::BeginTabItem("Log")) {
        drawLog(ctx);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Perf")) {
        drawPerf(ctx);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Jobs")) {
        drawJobs(ctx);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

} // namespace

PanelPtr makeLogPanel() {
    return std::make_unique<LogPanel>();
}

} // namespace qlab::ui
