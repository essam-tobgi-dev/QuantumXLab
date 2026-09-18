// Spec 17 §7.10 / 19 §3 "Guided tour" — the transport, the step list, the narration of the current
// stop with its theory link and live rows, and the narration card the Viewport draws over its
// picture while the tour plays. The panel owns no tour state: `lab::Tour` (headless) is the model.
#include "Lab/Tour.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <cctype>
#include <format>

namespace qlab::ui {
namespace {

using widgets::u32;

// "T08#1.2-cooling-power" → "T08 §1.2" (the Theory Browser opens the full anchor).
std::string tourAnchorLabel(std::string_view anchor) {
    const std::size_t hash = anchor.find('#');
    if (hash == std::string_view::npos) return std::string(anchor);
    const std::string_view rest = anchor.substr(hash + 1);
    std::size_t n = 0;
    while (n < rest.size() && (std::isdigit(static_cast<unsigned char>(rest[n])) || rest[n] == '.')) ++n;
    if (n == 0) return std::string(anchor.substr(0, hash));
    return std::string(anchor.substr(0, hash)) + " §" + std::string(rest.substr(0, n));
}

// One live row "field: value unit" with its fidelity badge (spec 00 §5); Simulator-only rows are
// hidden in Physical-lab mode (spec 19 §2).
void liveRow(const UiContext& ctx, const lab::Tooltip::LiveRow& row) {
    if (row.simulatorOnly && ctx.physicalLab) return;
    widgets::text(ctx, Token::TextPrimary, row.text);
    ImGui::SameLine();
    widgets::fidelityBadge(ctx, row.cls);
    if (row.simulatorOnly) {
        ImGui::SameLine();
        widgets::simOnlyBadge(ctx);
    }
}

class TourPanel final : public BasicPanel {
public:
    TourPanel() : BasicPanel(PanelId::Tour, "tour", "panels.tour", "▶", Workspace::Lab) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override { return core::Json::object({{"list_fraction", listFraction_}}); }
    void deserialize(const core::Json& j) override {
        if (!j.is_object()) return;
        if (const auto it = j.find("list_fraction"); it != j.end() && it->is_number())
            listFraction_ = std::clamp(it->get<float>(), 0.2f, 0.6f);
    }

private:
    void drawTransport(UiContext& ctx, lab::Tour& tour);
    void drawStepList(UiContext& ctx, lab::Tour& tour);
    void drawNarration(UiContext& ctx, lab::Tour& tour);

    float listFraction_ = 0.34f;
};

void TourPanel::draw(UiContext& ctx) {
    if (ctx.tour == nullptr || ctx.tour->size() == 0) {
        widgets::placeholder(ctx, "This layout ships no guided tour.");
        return;
    }
    lab::Tour& tour = *ctx.tour;
    drawTransport(ctx, tour);
    const Metrics m = ctx.metrics_px();
    const float width = ImGui::GetContentRegionAvail().x;
    if (ImGui::BeginChild("##tour_steps", ImVec2(width * listFraction_, 0.0f), ImGuiChildFlags_Borders)) drawStepList(ctx, tour);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, m.spacing(2));
    if (ImGui::BeginChild("##tour_body", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None)) drawNarration(ctx, tour);
    ImGui::EndChild();
}

void TourPanel::drawTransport(UiContext& ctx, lab::Tour& tour) {
    const Metrics m = ctx.metrics_px();
    const float h = ImGui::GetFrameHeight();
    const bool playing = tour.playing();
    if (widgets::primaryButton(ctx, playing ? "Pause" : "Play", ImVec2(ctx.ui(64.0f), h))) {
        if (playing) tour.pause();
        else tour.play();
    }
    ImGui::SameLine(0.0f, m.spacing(1));
    if (widgets::secondaryButton(ctx, "◀", ImVec2(h, h))) tour.prev();
    ImGui::SameLine(0.0f, m.spacing(1));
    if (widgets::secondaryButton(ctx, "▶", ImVec2(h, h))) tour.next();
    ImGui::SameLine(0.0f, m.spacing(1));
    ImGui::BeginDisabled(!tour.active());
    if (widgets::secondaryButton(ctx, "Stop", ImVec2(ctx.ui(56.0f), h))) tour.stop();
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, m.spacing(3));
    {
        FontScope strong(*ctx.fonts, FontRole::Strong);
        widgets::text(ctx, Token::TextPrimary, tour.title());
    }
    ImGui::SameLine(0.0f, m.spacing(2));
    widgets::text(ctx, Token::TextSecondary, std::format("step {} / {}", tour.step() + 1, tour.size()));
    if (tour.paused()) {
        ImGui::SameLine(0.0f, m.spacing(2));
        widgets::badge(ctx, "paused", ctx.th()[Token::Warn]);
    } else if (tour.finished()) {
        ImGui::SameLine(0.0f, m.spacing(2));
        widgets::badge(ctx, "finished", ctx.th()[Token::Ok]);
    }
    // The dwell progress: the flight is the first 0.9 s of every stop.
    const lab::TourStep& s = tour.current();
    const std::string overlay = tour.playing() ? std::format("{:.0f} s stop", s.dwell_s) : std::string{};
    widgets::progressBar(ctx, tour.stepProgress(), overlay);
}

void TourPanel::drawStepList(UiContext& ctx, lab::Tour& tour) {
    const std::span<const lab::TourStep> steps = tour.steps();
    for (std::size_t k = 0; k < steps.size(); ++k) {
        ImGui::PushID(static_cast<int>(k));
        const bool current = k == tour.step();
        const bool skipped = tour.stepSkipped(k);
        const std::string label = std::format("{:>2}  {}", k + 1, steps[k].title);
        if (skipped) ImGui::PushStyleColor(ImGuiCol_Text, widgets::iv(ctx.th()[Token::TextDisabled]));
        else if (current) ImGui::PushStyleColor(ImGuiCol_Text, widgets::iv(ctx.th()[Token::Accent]));
        if (ImGui::Selectable(label.c_str(), current) && !skipped) tour.seek(k); // click = seek
        if (skipped || current) ImGui::PopStyleColor();
        if (skipped && ImGui::IsItemHovered()) widgets::simOnlyDisabledTooltip(ctx);
        if (current && tour.playing()) ImGui::SetScrollHereY(0.5f);
        ImGui::PopID();
    }
}

void TourPanel::drawNarration(UiContext& ctx, lab::Tour& tour) {
    const Metrics m = ctx.metrics_px();
    const lab::TourStep& s = tour.current();
    {
        FontScope title(*ctx.fonts, FontRole::PanelTitle);
        widgets::text(ctx, Token::TextPrimary, s.title);
    }
    const std::string focus = tour.focusName();
    if (!focus.empty()) widgets::text(ctx, Token::TextSecondary, focus);
    ImGui::Dummy(ImVec2(0.0f, m.spacing(1)));
    {
        FontScope body(*ctx.fonts, FontRole::Body);
        widgets::textWrapped(ctx, Token::TextPrimary, s.narration);
    }
    ImGui::Dummy(ImVec2(0.0f, m.spacing(1)));
    if (!s.theory.empty()) {
        ImGui::BeginDisabled(!ctx.cmd.openTheory);
        if (widgets::secondaryButton(ctx, "Theory  " + tourAnchorLabel(s.theory)) && ctx.cmd.openTheory)
            ctx.cmd.openTheory(s.theory);
        ImGui::EndDisabled();
    }
    const std::vector<lab::Tooltip::LiveRow> rows = tour.liveRows();
    if (!rows.empty()) {
        widgets::sectionHeader(ctx, ctx.text("inspector.live"));
        for (const lab::Tooltip::LiveRow& row : rows) liveRow(ctx, row);
    }
}

} // namespace

PanelPtr makeTourPanel() { return std::make_unique<TourPanel>(); }

// Spec 17 §7.10 — the translucent narration card at the bottom of the picture while a tour is
// active (playing or paused); nothing when no tour is loaded or it is idle.
void drawTourOverlay(UiContext& ctx, ImVec2 imageMin, ImVec2 imageMax) {
    if (ctx.tour == nullptr || !ctx.tour->active() || ctx.tour->size() == 0) return;
    const lab::Tour& tour = *ctx.tour;
    const lab::TourStep& s = tour.current();
    const Metrics m = ctx.metrics_px();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // A compact card along the RIGHT edge of the picture, so the machine the narration talks
    // about stays in view (the full text is in the Guided Tour panel). Text is drawn at the
    // faces' DISPLAY size: `ImFont::FontSize` is the rasterised size, dpi× too large on a
    // Retina display (spec 19 §7), so it is scaled by io.FontGlobalScale here.
    const float scale = ImGui::GetIO().FontGlobalScale;
    ImFont* body = ctx.fonts != nullptr ? ctx.fonts->get(FontRole::Secondary) : nullptr;
    ImFont* strong = ctx.fonts != nullptr ? ctx.fonts->get(FontRole::Strong) : nullptr;
    if (body == nullptr) body = ImGui::GetFont();
    if (strong == nullptr) strong = body;
    const float bodyPx = body->FontSize * scale, strongPx = strong->FontSize * scale;
    const float pad = m.spacing(2);
    const float imageW = std::max(imageMax.x - imageMin.x, ctx.ui(200.0f));
    const float boxW = std::clamp(imageW * 0.28f, ctx.ui(220.0f), ctx.ui(300.0f));
    const float wrapW = boxW - 2.0f * pad;
    const std::string head = s.title;
    const std::string step = std::format("step {} / {}", tour.step() + 1, tour.size());
    const std::string focus = tour.focusName();
    const std::string theory = s.theory.empty() ? std::string{} : "Theory " + tourAnchorLabel(s.theory);
    const ImVec2 headSize = strong->CalcTextSizeA(strongPx, FLT_MAX, wrapW, head.c_str());
    const ImVec2 textSize = body->CalcTextSizeA(bodyPx, FLT_MAX, wrapW, s.narration.c_str());
    const float lineH = bodyPx * 1.35f;
    // Never taller than 55 % of the picture: the narration is clipped there (it is complete in
    // the panel below), so the card cannot hide the model.
    const float maxH = (imageMax.y - imageMin.y) * 0.55f;
    float boxH = 2.0f * pad + headSize.y + lineH + (focus.empty() ? 0.0f : lineH) + m.spacing(1) + textSize.y +
                 (theory.empty() ? 0.0f : lineH + m.spacing(1)) + 4.0f;
    boxH = std::min(boxH, maxH);
    const ImVec2 a(imageMax.x - boxW - m.spacing(4), imageMin.y + m.spacing(4));
    const ImVec2 b(a.x + boxW, a.y + boxH);
    Color bg = ctx.th()[Token::BgPanel];
    bg.a = 0.80f;
    dl->AddRectFilled(a, b, u32(bg), m.radiusMd);
    dl->AddRect(a, b, u32(ctx.th()[tour.paused() ? Token::Warn : Token::Accent]), m.radiusMd);
    dl->PushClipRect(a, ImVec2(b.x, b.y - 4.0f), true);
    float y = a.y + pad;
    dl->AddText(strong, strongPx, ImVec2(a.x + pad, y), u32(ctx.th()[Token::TextPrimary]), head.c_str(), nullptr, wrapW);
    y += headSize.y;
    dl->AddText(body, bodyPx, ImVec2(a.x + pad, y), u32(ctx.th()[Token::TextSecondary]), step.c_str());
    y += lineH;
    if (!focus.empty()) {
        dl->AddText(body, bodyPx, ImVec2(a.x + pad, y), u32(ctx.th()[Token::TextSecondary]), focus.c_str(), nullptr, wrapW);
        y += lineH;
    }
    y += m.spacing(1);
    dl->AddText(body, bodyPx, ImVec2(a.x + pad, y), u32(ctx.th()[Token::TextPrimary]), s.narration.c_str(), nullptr, wrapW);
    y += textSize.y + m.spacing(1);
    if (!theory.empty()) dl->AddText(body, bodyPx, ImVec2(a.x + pad, y), u32(ctx.th()[Token::Accent]), theory.c_str());
    dl->PopClipRect();
    // The dwell progress as a thin accent line along the card's lower edge.
    const float progressW = (boxW - 2.0f * pad) * static_cast<float>(std::clamp(tour.stepProgress(), 0.0, 1.0));
    dl->AddLine(ImVec2(a.x + pad, b.y - 2.0f), ImVec2(a.x + pad + progressW, b.y - 2.0f), u32(ctx.th()[Token::Accent]), 2.0f);
}

} // namespace qlab::ui
