#pragma once
// Spec 20 §5 — `ui::EquationView`: a laid-out LaTeX equation drawn in a panel, with the symbol
// boxes live. Hover underlines the symbol in `accent` and opens an `EduTooltip` with the term name,
// its unit, its current live value and the equation's assumption list; click opens the Theory
// Browser at the equation's anchor; Ctrl+click copies the LaTeX; the context menu offers
// copy LaTeX / copy plain / open theory. `withValues` adds a second line of `symbol = value unit`.
//
// Spec 19 §6 — `EduTooltip` is the same card without an owning widget: title, one-sentence meaning,
// the governing equation, the current value with unit and fidelity badge, and a "Theory ›" link.
#include "Data/Fidelity.hpp"
#include "UI/Context.hpp"
#include "UI/Widgets/Equations.hpp"
#include <functional>
#include <imgui.h>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::ui {

// Live value of one symbol of an equation, supplied by whatever panel draws it (the Inspector
// resolves them through `lab::BindingRegistry`, the Estimates panel from the estimate record).
struct TermValue {
    std::string symbol; // matches EquationTerm::symbol
    double si = 0.0;
    std::string unit; // overrides the term's declared unit when non-empty
    data::FidelityClass cls = data::FidelityClass::Model;
    bool simulatorOnly = false;
    bool available = true; // false renders "—" (spec 17 §5)
};

class EquationView {
  public:
    // `latex` may come from an `EquationDoc` or straight from a component descriptor.
    void setLatex(std::string latex);
    void setDocument(const EquationDoc* doc); // supplies terms, assumptions and the theory anchor
    void setValues(std::vector<TermValue> values);
    void setWithValues(bool on) { withValues_ = on; } // spec 20 §5 second line
    void setDisplayStyle(bool display) { display_ = display; }
    void setScale(float s) { scale_ = s; } // ×1 body size; the Theory Browser uses 1.15

    const std::string& latex() const { return latex_; }
    const EquationDoc* document() const { return doc_; }
    // Symbol under a position relative to the equation's top-left; empty when none.
    std::string symbolAt(const UiContext& ctx, ImVec2 local) const;
    // Size the equation will occupy at the current scale, in ImGui units (0,0 before the first
    // layout or without a math renderer).
    ImVec2 size(const UiContext& ctx) const;

    // Draws at the current cursor. Returns the symbol that was clicked, or "" when nothing was.
    std::string draw(const UiContext& ctx);

  private:
    const math::LayoutResult* layout(const UiContext& ctx) const;
    const TermValue* valueOf(std::string_view symbol) const;

    std::string latex_;
    const EquationDoc* doc_ = nullptr;
    std::vector<TermValue> values_;
    bool withValues_ = false, display_ = true;
    float scale_ = 1.0f;
    mutable std::shared_ptr<const math::LayoutResult> cached_;
    mutable std::string cachedKey_;
    mutable float cachedSize_ = 0.0f;
};

// Spec 19 §6 — the educational tooltip. `id` is an equation id; the title and meaning come from the
// strings table (`tooltips.<id>.title` / `.meaning`) and fall back to the term/equation text.
// Delay 250 ms (ImGui's `DelayNormal`), max width 420 px, hidden on any key press.
void eduTooltip(const UiContext& ctx, std::string_view id, std::span<const TermValue> values = {});
// The card body without the hover plumbing, for panels that show it inline.
void eduCard(const UiContext& ctx, std::string_view id, std::string_view title,
             std::string_view meaning, std::span<const TermValue> values);

} // namespace qlab::ui
