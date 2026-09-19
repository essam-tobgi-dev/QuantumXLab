// Spec 20 §5 / 19 §6 — interactive equations and the educational tooltip (see EquationView.hpp).
#include "UI/Widgets/EquationView.hpp"
#include "UI/Format.hpp"
#include "UI/Widgets/MathImGui.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <format>

namespace qlab::ui {
namespace {

using widgets::iv;
using widgets::u32;

constexpr float kTooltipWidthPx = 420.0f; // spec 19 §6

math::MathStyle styleFor(const UiContext& ctx, bool display, float scale) {
    math::MathStyle s;
    s.display = display;
    s.sizePx =
        std::max(8.0f, ctx.metrics_px().bodyPx * std::max(0.5f, scale) * (display ? 1.15f : 1.0f));
    return s;
}

} // namespace

void EquationView::setLatex(std::string latex) {
    if (latex == latex_)
        return;
    latex_ = std::move(latex);
    cached_.reset();
    cachedKey_.clear();
}

void EquationView::setDocument(const EquationDoc* doc) {
    doc_ = doc;
    if (doc != nullptr)
        setLatex(doc->latex);
}

void EquationView::setValues(std::vector<TermValue> values) {
    values_ = std::move(values);
}

const TermValue* EquationView::valueOf(std::string_view symbol) const {
    for (const TermValue& v : values_)
        if (v.symbol == symbol)
            return &v;
    return nullptr;
}

const math::LayoutResult* EquationView::layout(const UiContext& ctx) const {
    if (ctx.math == nullptr || !ctx.math->ready() || latex_.empty())
        return nullptr;
    const math::MathStyle style = styleFor(ctx, display_, scale_);
    const std::string key = std::format("{}|{}|{}", latex_, style.sizePx, style.display);
    if (cached_ != nullptr && key == cachedKey_)
        return cached_.get();
    auto r = ctx.math->renderer().render(latex_, style);
    if (!r)
        return nullptr;
    cached_ = *r;
    cachedKey_ = key;
    cachedSize_ = static_cast<float>(style.sizePx);
    return cached_.get();
}

ImVec2 EquationView::size(const UiContext& ctx) const {
    const math::LayoutResult* lr = layout(ctx);
    if (lr == nullptr)
        return ImVec2(0.0f, 0.0f);
    return ImVec2(static_cast<float>(lr->width), static_cast<float>(lr->height + lr->depth));
}

std::string EquationView::symbolAt(const UiContext& ctx, ImVec2 local) const {
    const math::LayoutResult* lr = layout(ctx);
    if (lr == nullptr)
        return {};
    const auto hit = math::hitTestMath(*lr, 0.0, lr->height, local.x, local.y);
    return hit ? hit->text : std::string{};
}

std::string EquationView::draw(const UiContext& ctx) {
    const math::LayoutResult* lr = layout(ctx);
    if (lr == nullptr) {
        // Spec 20 §7: a placeholder of the estimated size rather than a blank hole.
        widgets::text(ctx, Token::TextSecondary,
                      latex_.empty() ? std::string_view{} : std::string_view(latex_));
        return {};
    }
    const auto w = static_cast<float>(lr->width);
    const auto h = static_cast<float>(lr->height + lr->depth);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##equation", ImVec2(std::max(w, 1.0f), std::max(h, 1.0f)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiMathCanvas canvas(dl, ctx.math->font(), origin, u32(ctx.th()[Token::TextPrimary]));
    math::paintMath(*lr, canvas, 0.0, lr->height, ctx.math->font());

    std::string clicked;
    std::string hoverSymbol;
    if (hovered) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const auto hit =
            math::hitTestMath(*lr, 0.0, lr->height, mouse.x - origin.x, mouse.y - origin.y);
        if (hit) {
            hoverSymbol = hit->text;
            // Spec 20 §5: accent underline under the hovered symbol box.
            const float y = origin.y + static_cast<float>(hit->y + hit->h) + 1.0f;
            dl->AddLine(ImVec2(origin.x + static_cast<float>(hit->x), y),
                        ImVec2(origin.x + static_cast<float>(hit->x + hit->w), y),
                        u32(ctx.th()[Token::Accent]), ctx.metrics_px().border);
        }
    }

    if (hovered && !hoverSymbol.empty()) {
        const EquationTerm* term = doc_ != nullptr ? doc_->term(hoverSymbol) : nullptr;
        if (ImGui::BeginTooltip()) {
            ImGui::PushTextWrapPos(ctx.ui(kTooltipWidthPx));
            widgets::text(ctx, Token::TextPrimary,
                          term != nullptr ? std::string_view(term->name)
                                          : std::string_view(hoverSymbol));
            if (const TermValue* v = valueOf(hoverSymbol); v != nullptr) {
                const std::string unit = v->unit.empty() && term != nullptr ? term->unit : v->unit;
                widgets::labelled(ctx, "=", v->available ? format::value(v->si, unit) : "—");
                ImGui::SameLine();
                widgets::fidelityBadge(ctx, v->cls);
                if (v->simulatorOnly) {
                    ImGui::SameLine();
                    widgets::simOnlyBadge(ctx);
                }
            } else if (term != nullptr && !term->unit.empty()) {
                widgets::labelled(ctx, ctx.text("inspector.spec_sheet"), term->unit);
            }
            if (doc_ != nullptr && ctx.assets != nullptr && !doc_->assumptions.empty()) {
                widgets::text(ctx, Token::TextSecondary, ctx.text("estimates.assumptions"));
                for (const std::string& key : doc_->assumptions) {
                    const std::string_view sentence = ctx.assets->assumption(key);
                    widgets::text(ctx, Token::TextSecondary,
                                  sentence.empty() ? std::string_view(key) : sentence);
                }
            }
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        if (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper)
            ImGui::SetClipboardText(latex_.c_str());
        else if (doc_ != nullptr && !doc_->theory.empty() && ctx.cmd.openTheory)
            ctx.cmd.openTheory(doc_->theory);
        clicked = hoverSymbol;
    }
    if (ImGui::BeginPopupContextItem("##equation_menu")) {
        if (ImGui::MenuItem("Copy LaTeX"))
            ImGui::SetClipboardText(latex_.c_str());
        if (doc_ != nullptr && ImGui::MenuItem("Copy plain"))
            ImGui::SetClipboardText(doc_->plain.c_str());
        const bool hasTheory = doc_ != nullptr && !doc_->theory.empty();
        if (ImGui::MenuItem("Open theory", nullptr, false, hasTheory) && ctx.cmd.openTheory)
            ctx.cmd.openTheory(doc_->theory);
        ImGui::EndPopup();
    }

    // Spec 20 §5: `withValues` renders `symbol = value unit` on a second line.
    if (withValues_ && !values_.empty()) {
        FontScope f(*ctx.fonts, FontRole::Secondary);
        std::string line;
        for (const TermValue& v : values_) {
            const EquationTerm* term = doc_ != nullptr ? doc_->term(v.symbol) : nullptr;
            const std::string unit = v.unit.empty() && term != nullptr ? term->unit : v.unit;
            if (!line.empty())
                line += "   ";
            line += v.symbol + " = " + (v.available ? format::value(v.si, unit) : "—");
        }
        widgets::text(ctx, Token::TextSecondary, line);
    }
    return clicked;
}

// ---------------------------------------------------------------- EduTooltip (spec 19 §6)

void eduCard(const UiContext& ctx, std::string_view id, std::string_view title,
             std::string_view meaning, std::span<const TermValue> values) {
    ImGui::PushTextWrapPos(ctx.ui(kTooltipWidthPx));
    {
        FontScope f(*ctx.fonts, FontRole::Strong);
        widgets::text(ctx, Token::TextPrimary, title.empty() ? id : title);
    }
    if (!meaning.empty())
        widgets::textWrapped(ctx, Token::TextSecondary, meaning);

    const EquationDoc* doc = ctx.assets != nullptr ? ctx.assets->equation(id) : nullptr;
    if (doc != nullptr) {
        EquationView eq;
        eq.setDocument(doc);
        eq.setDisplayStyle(false);
        eq.setValues(std::vector<TermValue>(values.begin(), values.end()));
        eq.draw(ctx);
    }
    for (const TermValue& v : values) {
        const EquationTerm* term = doc != nullptr ? doc->term(v.symbol) : nullptr;
        const std::string unit = v.unit.empty() && term != nullptr ? term->unit : v.unit;
        widgets::labelled(
            ctx, term != nullptr ? std::string_view(term->name) : std::string_view(v.symbol),
            v.available ? format::value(v.si, unit) : "—");
        ImGui::SameLine();
        widgets::fidelityBadge(ctx, v.cls);
        if (v.simulatorOnly) {
            ImGui::SameLine();
            widgets::simOnlyBadge(ctx);
        }
    }
    if (doc != nullptr && !doc->theory.empty()) {
        widgets::text(ctx, Token::Accent, "Theory ›");
        if (ImGui::IsItemClicked() && ctx.cmd.openTheory)
            ctx.cmd.openTheory(doc->theory);
    }
    ImGui::PopTextWrapPos();
}

void eduTooltip(const UiContext& ctx, std::string_view id, std::span<const TermValue> values) {
    // Spec 19 §6: 250 ms delay, hidden on any key press.
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        return;
    if (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive())
        return;
    if (!ImGui::BeginTooltip())
        return;
    const std::string titleKey = std::string("tooltips.") + std::string(id) + ".title";
    const std::string meaningKey = std::string("tooltips.") + std::string(id) + ".meaning";
    const Strings& s = Strings::global();
    eduCard(ctx, id, s.has(titleKey) ? s.get(titleKey) : std::string_view{},
            s.has(meaningKey) ? s.get(meaningKey) : std::string_view{}, values);
    ImGui::EndTooltip();
}

} // namespace qlab::ui
