// Spec 19 §3 "Theory Browser" / spec 20 §6 — renders `docs/theory/*.md` through the finished
// `ui::theory` parser: a table-of-contents sidebar, anchor navigation, back / forward history, a
// 720 px measure, display equations centred with their `\tag` right-aligned, fenced code in
// JetBrains Mono with a copy button, and "copy as LaTeX" on every equation.
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/EquationView.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <imgui.h>

namespace qlab::ui {
namespace {

constexpr float kMeasurePx = 720.0f; // spec 20 §6

class TheoryPanel final : public BasicPanel {
public:
    TheoryPanel() : BasicPanel(PanelId::Theory, "theory", "panels.theory", "§", Workspace::Analysis) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["doc"] = doc_;
        j["anchor"] = anchor_;
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (!j.is_object()) return;
        if (const auto it = j.find("doc"); it != j.end() && it->is_string()) doc_ = it->get<std::string>();
        if (const auto it = j.find("anchor"); it != j.end() && it->is_string()) anchor_ = it->get<std::string>();
    }

private:
    void navigate(std::string doc, std::string anchor);
    void drawSpans(UiContext& ctx, const std::vector<theory::InlineSpan>& spans);
    void drawBlock(UiContext& ctx, const theory::Block& block, std::size_t index);

    std::string doc_ = "T01", anchor_;
    std::vector<std::pair<std::string, std::string>> back_, forward_;
    std::string scrollToAnchor_;
    std::map<std::size_t, EquationView> equations_;
};

void TheoryPanel::navigate(std::string doc, std::string anchor) {
    if (doc == doc_ && anchor == anchor_) return;
    back_.emplace_back(doc_, anchor_);
    forward_.clear();
    doc_ = std::move(doc);
    anchor_ = std::move(anchor);
    scrollToAnchor_ = anchor_;
    equations_.clear();
}

void TheoryPanel::drawSpans(UiContext& ctx, const std::vector<theory::InlineSpan>& spans) {
    bool first = true;
    for (const theory::InlineSpan& s : spans) {
        if (!first) ImGui::SameLine(0.0f, 0.0f);
        first = false;
        if (s.math) {
            EquationView inlineEq;
            inlineEq.setLatex(s.text);
            inlineEq.setDisplayStyle(false);
            inlineEq.draw(ctx);
            continue;
        }
        if (s.link.kind != theory::LinkKind::None) {
            widgets::text(ctx, Token::Accent, s.text);
            if (ImGui::IsItemClicked()) {
                if (s.link.kind == theory::LinkKind::Theory) navigate(s.link.doc, s.link.anchor);
                else if (s.link.kind == theory::LinkKind::Internal) scrollToAnchor_ = s.link.anchor;
                else if (s.link.kind == theory::LinkKind::Component && ctx.interaction != nullptr && ctx.scene != nullptr) {
                    // spec 20 §6: `component:` selects the component and focuses the camera.
                    const ComponentId id = ctx.interaction->searchSelect(s.link.id, ctx.camera);
                    if (ctx.cmd.selectComponent) ctx.cmd.selectComponent(id);
                }
            }
            continue;
        }
        if (s.code) {
            FontScope f(*ctx.fonts, FontRole::Code);
            widgets::text(ctx, Token::ClassStatistical, s.text);
            continue;
        }
        FontScope f(*ctx.fonts, s.bold ? FontRole::Strong : FontRole::Body);
        widgets::text(ctx, Token::TextPrimary, s.text);
    }
}

void TheoryPanel::drawBlock(UiContext& ctx, const theory::Block& block, std::size_t index) {
    using theory::BlockKind;
    switch (block.kind) {
    case BlockKind::Heading: {
        if (!block.anchor.empty() && block.anchor == scrollToAnchor_) {
            ImGui::SetScrollHereY(0.05f);
            scrollToAnchor_.clear();
        }
        FontScope f(*ctx.fonts, block.level <= 2 ? FontRole::PanelTitle : FontRole::Strong);
        widgets::text(ctx, Token::TextPrimary, block.plain);
        break;
    }
    case BlockKind::Paragraph:
    case BlockKind::Quote:
        ImGui::PushTextWrapPos(ctx.ui(kMeasurePx));
        drawSpans(ctx, block.spans);
        ImGui::PopTextWrapPos();
        break;
    case BlockKind::DisplayMath: {
        EquationView& eq = equations_[index];
        eq.setLatex(block.latex);
        if (ctx.assets != nullptr)
            for (const EquationDoc& d : ctx.assets->equations())
                if (d.latex == block.latex) eq.setDocument(&d);
        eq.setDisplayStyle(true);
        const float width = eq.size(ctx).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             std::max(0.0f, (std::min(ctx.ui(kMeasurePx), ImGui::GetContentRegionAvail().x) - width) * 0.5f));
        eq.draw(ctx);
        if (!block.tag.empty()) {
            ImGui::SameLine();
            widgets::text(ctx, Token::TextSecondary, "(" + block.tag + ")");
        }
        break;
    }
    case BlockKind::Code: {
        FontScope f(*ctx.fonts, FontRole::Code);
        widgets::text(ctx, Token::TextSecondary, block.plain);
        ImGui::PushID(static_cast<int>(index));
        if (widgets::secondaryButton(ctx, "Copy")) ImGui::SetClipboardText(block.plain.c_str());
        ImGui::PopID();
        break;
    }
    case BlockKind::List:
        for (const theory::ListItem& item : block.items) {
            ImGui::Indent(static_cast<float>(item.indent + 1) * ctx.metrics_px().spacing(3));
            widgets::text(ctx, Token::TextSecondary, item.marker);
            ImGui::SameLine();
            drawSpans(ctx, item.spans);
            ImGui::Unindent(static_cast<float>(item.indent + 1) * ctx.metrics_px().spacing(3));
        }
        break;
    case BlockKind::Table: {
        if (block.rows.empty()) break;
        const auto columns = static_cast<int>(block.rows.front().size());
        if (columns <= 0 || !ImGui::BeginTable("##t", columns, widgets::tableFlags(false))) break;
        for (std::size_t r = 0; r < block.rows.size(); ++r) {
            ImGui::TableNextRow();
            for (const theory::TableCell& cell : block.rows[r]) {
                ImGui::TableNextColumn();
                widgets::text(ctx, r == 0 ? Token::TextPrimary : Token::TextSecondary, cell.plain);
            }
        }
        ImGui::EndTable();
        break;
    }
    case BlockKind::Rule:
        ImGui::Separator();
        break;
    }
}

void TheoryPanel::draw(UiContext& ctx) {
    if (ctx.theoryIndex == nullptr) {
        widgets::placeholder(ctx, "The theory corpus is not loaded.");
        return;
    }
    // The Inspector, the views and the equations all open the browser through this command.
    if (!ctx.cmd.openTheory)
        ctx.cmd.openTheory = [this](std::string_view ref) {
            const std::size_t hash = ref.find('#');
            navigate(std::string(ref.substr(0, hash)),
                     hash == std::string_view::npos ? std::string{} : std::string(ref.substr(hash + 1)));
        };

    {
        widgets::DisabledScope guard(ctx, back_.empty());
        if (widgets::secondaryButton(ctx, "←") && !back_.empty()) {
            forward_.emplace_back(doc_, anchor_);
            std::tie(doc_, anchor_) = back_.back();
            back_.pop_back();
            scrollToAnchor_ = anchor_;
        }
    }
    ImGui::SameLine();
    {
        widgets::DisabledScope guard(ctx, forward_.empty());
        if (widgets::secondaryButton(ctx, "→") && !forward_.empty()) {
            back_.emplace_back(doc_, anchor_);
            std::tie(doc_, anchor_) = forward_.back();
            forward_.pop_back();
            scrollToAnchor_ = anchor_;
        }
    }
    ImGui::SameLine();
    widgets::text(ctx, Token::TextSecondary, doc_ + (anchor_.empty() ? "" : "#" + anchor_));
    ImGui::Separator();

    const theory::TheoryDocument* document = ctx.theoryIndex->document(doc_);
    // ---- table of contents
    ImGui::BeginChild("##toc", ImVec2(ctx.ui(200.0f), 0.0f), ImGuiChildFlags_Borders);
    for (const std::string& id : ctx.theoryIndex->documentIds()) {
        const bool current = id == doc_;
        widgets::text(ctx, current ? Token::Accent : Token::TextSecondary, id);
        if (ImGui::IsItemClicked()) navigate(id, {});
        if (!current || document == nullptr) continue;
        for (const theory::HeadingRef& h : document->headings) {
            if (h.level > 2) continue;
            ImGui::Indent(ctx.metrics_px().spacing(2));
            widgets::text(ctx, Token::TextSecondary, h.text);
            if (ImGui::IsItemClicked()) scrollToAnchor_ = h.anchor;
            ImGui::Unindent(ctx.metrics_px().spacing(2));
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- document body
    ImGui::BeginChild("##body", ImVec2(0.0f, 0.0f));
    if (document == nullptr) {
        widgets::placeholder(ctx, "Document '" + doc_ + "' was not found.");
    } else {
        for (std::size_t i = 0; i < document->blocks.size(); ++i) drawBlock(ctx, document->blocks[i], i);
    }
    ImGui::EndChild();
}

} // namespace

PanelPtr makeTheoryPanel() { return std::make_unique<TheoryPanel>(); }

} // namespace qlab::ui
