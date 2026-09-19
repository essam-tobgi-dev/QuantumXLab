#pragma once
// Spec 19 §5.1–§5.3 — the numeric field every panel edits values through:
//  1. the unit is always visible as a suffix in `text.secondary`, with SI-prefix auto-selection;
//  2. drag to edit (Shift ×0.1 fine, Alt ×10 coarse), double-click to type any unit of the same
//     dimension, scroll over a hovered field steps by `step`;
//  3. every committed edit goes onto `ctx.undo`, grouped so one drag is one undo entry;
//  4. the value's fidelity class shows on hover, and a Simulator-only value is drawn in `sim_only`.
#include "Data/Fidelity.hpp"
#include "UI/Context.hpp"
#include <imgui.h>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace qlab::ui::widgets {

struct FieldSpec {
    std::string unit;  // catalog symbol; "" = dimensionless
    double step = 0.0; // value change per drag pixel; 0 = auto
    double lo = -std::numeric_limits<double>::infinity();
    double hi = std::numeric_limits<double>::infinity();
    int digits = 4;
    data::FidelityClass cls = data::FidelityClass::Model;
    bool simulatorOnly = false;
    bool readOnly = false;
    std::string tooltipId; // `ui::EduTooltip` id (spec 19 §6)
    std::string undoLabel; // shown in the Edit menu; defaults to the label
    float width = 0.0f;    // logical points; 0 = 55 % of the available width (a form)
};

// Draws `label` then the field. Returns true on the frames where `*value` changed.
bool numberField(const UiContext& ctx, std::string_view label, double* value,
                 const FieldSpec& spec);
// Integer variant (shots, seed, qubit index): the same behaviour with integral steps.
bool intField(const UiContext& ctx, std::string_view label, std::int64_t* value,
              const FieldSpec& spec);
// Read-only readout in JetBrains Mono tabular figures with the unit suffix and the class badge.
void readout(const UiContext& ctx, std::string_view label, double si, const FieldSpec& spec);
// A labelled combo over `items`; records the change on the undo stack.
bool combo(const UiContext& ctx, std::string_view label, int* index,
           std::span<const std::string_view> items, std::string_view undoLabel = {});
// A labelled checkbox that records the change on the undo stack.
bool checkbox(const UiContext& ctx, std::string_view label, bool* value,
              std::string_view undoLabel = {});

} // namespace qlab::ui::widgets
