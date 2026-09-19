#pragma once
// INTERNAL to the circuit diagram (CircuitView.cpp ↔ CircuitViewDraw.cpp ↔ CircuitViewSvg.cpp);
// not part of Viz.hpp. Glyph geometry in layout units (wire pitch = 1) so that the GL scene and the
// SVG export of spec 23 §8 draw the same picture, and the CPU hit test agrees with both.
#include "Viz/Layout/CircuitLayout.hpp"
#include <algorithm>
#include <string>

namespace qlab::viz::detail {

// Layout units kept free left of x = 0 for the wire labels ("q[12] → 7").
inline constexpr double kLabelGutter = 1.25;
// Marks on a wire.
inline constexpr double kControlDot = 0.085; // radius of a control dot
inline constexpr double kTargetRing = 0.19;  // radius of the ⊕ of a CX
inline constexpr double kSwapArm = 0.13;     // half diagonal of a SWAP ×
inline constexpr double kClassicalGap =
    0.035; // half separation of the two lines of a classical wire

inline constexpr double kBoxHalf = 0.35; // half height of a gate box (CircuitLayout.cpp)
inline constexpr double kTimeAxisBand =
    0.8; // band under the wires holding the ns axis (timed layout)

// Everything the diagram draws, in layout units: the wires, the label gutter, the headroom the
// timed layout's virtual-gate ticks are labelled in, and the time axis under the last wire.
inline Rect contentExtent(const layout::CircuitLayout& lay) {
    return {-kLabelGutter, lay.timed ? -0.55 : 0.0, lay.width,
            lay.height + (lay.timed ? kTimeAxisBand : 0.0)};
}

// Box a gate draws on its TARGET wires in the moment layout (`Glyph::bounds` also spans the
// control dots and, for a measurement, the classical wire it reaches down to).
inline Rect targetBox(const layout::CircuitLayout& lay, const layout::Glyph& g) {
    if (g.targetRows.empty())
        return g.bounds;
    std::uint32_t lo = g.targetRows.front(), hi = lo;
    for (std::uint32_t r : g.targetRows) {
        lo = std::min(lo, r);
        hi = std::max(hi, r);
    }
    return {g.bounds.x0, lay.rowY(lo) - kBoxHalf, g.bounds.x1, lay.rowY(hi) + kBoxHalf};
}

// Text of a wire label (spec 21 §3.13: register name and index; the physical view adds the
// physical index). `ir::Circuit::wireName` gives "q[2]" for a virtual wire and "$5" for a physical
// one, so the physical view prints "q2 → $5" wherever the layout map says which program qubit
// starts on that wire.
inline std::string wireLabelText(const layout::WireLabel& w) {
    std::string s = w.name.empty() ? std::string("q") : w.name;
    if (w.classical)
        return w.bits > 0 ? s + " /" + std::to_string(w.bits) : s;
    if (w.physical && w.virtualQubit)
        return "q" + std::to_string(*w.virtualQubit) + " \xE2\x86\x92 " + s;
    return s;
}

} // namespace qlab::viz::detail
