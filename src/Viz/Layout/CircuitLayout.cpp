// Spec 21 §3.13 — circuit layout: moments → columns with overlap-free packing, nested regions,
// the timed variant, and the queries the view needs (glyphAt, firstOverlap).
#include "Viz/Layout/CircuitLayoutImpl.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz::layout {
namespace {

constexpr double kBoxHalf = 0.35; // half height of a gate box (wire pitch = 1)
constexpr double kColumnGap = 0.30;
constexpr double kMargin = 0.35;

// Glyphs of one circuit level with columns counted from 0, plus the width of each column.
struct Level {
    std::vector<Glyph> glyphs;
    std::vector<double> widths;
    std::size_t moments = 0;
};

// `inheritedTopo` is the playhead index of the enclosing top-level node; it is used at depth > 0,
// where every glyph of a body lights up with the control node that owns it.
Level layoutLevel(const ir::Circuit& top, const ir::Circuit& c, std::uint32_t depth,
                  std::uint32_t qubitRows, std::uint32_t totalRows, std::uint32_t inheritedTopo) {
    Level level;
    const auto layers = c.layers();
    level.moments = layers.size();
    // Position of each node in the top-level topological order (the playhead unit).
    std::vector<std::uint32_t> orderOf;
    if (depth == 0) {
        std::uint32_t k = 0;
        for (ir::NodeId id : c.topologicalOrder()) {
            if (orderOf.size() <= id.get())
                orderOf.resize(id.get() + 1, 0);
            orderOf[id.get()] = k++;
        }
    }
    for (std::size_t m = 0; m < layers.size(); ++m) {
        const std::size_t base = level.widths.size();
        std::vector<std::vector<bool>> busy; // busy[subColumn][row]
        const auto place = [&](std::uint32_t lo, std::uint32_t hi, std::size_t span,
                               bool exclusive) {
            std::size_t col = exclusive ? busy.size() : 0;
            for (; col < busy.size(); ++col) {
                bool free = true;
                for (std::uint32_t r = lo; r <= hi && free; ++r)
                    free = !busy[col][r];
                if (free)
                    break;
            }
            while (busy.size() < col + span)
                busy.emplace_back(totalRows, false);
            for (std::size_t s = 0; s < span; ++s)
                for (std::uint32_t r = lo; r <= hi; ++r)
                    busy[col + s][r] = true;
            return col;
        };
        for (ir::NodeId id : layers[m]) {
            const ir::Node& node = c.node(id);
            auto described = detail::describeNode(top, node, qubitRows);
            if (!described)
                continue;
            Glyph g = std::move(*described);
            g.node = id;
            g.depth = depth;
            g.moment = static_cast<std::uint32_t>(m);
            g.topoIndex = depth == 0 ? orderOf[id.get()] : inheritedTopo;
            g.rowMax = std::min(g.rowMax, totalRows - 1);
            if (g.kind != GlyphKind::Region) {
                const std::size_t col = place(g.rowMin, g.rowMax, 1, false);
                g.column = static_cast<std::uint32_t>(base + col);
                if (level.widths.size() <= base + col)
                    level.widths.resize(base + col + 1, 0.0);
                level.widths[base + col] =
                    std::max(level.widths[base + col], detail::glyphWidth(g));
                level.glyphs.push_back(std::move(g));
                continue;
            }
            // A region owns fresh columns: its bodies side by side, an "else" separator between
            // them.
            std::vector<Level> bodies;
            std::vector<std::string> separators;
            std::size_t span = 0;
            for (const auto& body : detail::bodiesOf(node)) {
                bodies.push_back(
                    layoutLevel(top, *body.circuit, depth + 1, qubitRows, totalRows, g.topoIndex));
                separators.push_back(body.separator);
                span += std::max<std::size_t>(1, bodies.back().widths.size());
            }
            span = std::max<std::size_t>(1, span);
            for (const Level& b : bodies)
                for (const Glyph& child : b.glyphs) {
                    g.rowMin = std::min(g.rowMin, child.rowMin);
                    g.rowMax = std::max(g.rowMax, child.rowMax);
                }
            const std::size_t col = place(g.rowMin, g.rowMax, span, true);
            g.column = static_cast<std::uint32_t>(base + col);
            g.columnSpan = static_cast<std::uint32_t>(span);
            if (level.widths.size() < base + col + span)
                level.widths.resize(base + col + span, 0.0);
            // The header must fit over the region's columns.
            const double headerWidth = 0.4 + 0.13 * static_cast<double>(g.label.size());
            // Where the second body of a branch starts: the view draws the "else" divider there.
            if (bodies.size() > 1 && !separators[1].empty())
                g.elseColumn = static_cast<std::uint32_t>(
                    base + col + std::max<std::size_t>(1, bodies[0].widths.size()));
            level.glyphs.push_back(g);
            std::size_t offset = base + col;
            double bodyWidth = 0.0;
            for (std::size_t b = 0; b < bodies.size(); ++b) {
                const std::size_t cols = std::max<std::size_t>(1, bodies[b].widths.size());
                for (std::size_t k = 0; k < cols; ++k) {
                    const double w =
                        k < bodies[b].widths.size() ? std::max(bodies[b].widths[k], 0.7) : 0.7;
                    level.widths[offset + k] = std::max(level.widths[offset + k], w);
                    bodyWidth += level.widths[offset + k] + kColumnGap;
                }
                for (Glyph child : bodies[b].glyphs) {
                    child.column += static_cast<std::uint32_t>(offset);
                    if (child.elseColumn)
                        *child.elseColumn += static_cast<std::uint32_t>(offset);
                    level.glyphs.push_back(std::move(child));
                }
                offset += cols;
            }
            if (headerWidth > bodyWidth)
                level.widths[base + col + span - 1] += headerWidth - bodyWidth;
        }
    }
    return level;
}

void assignBounds(CircuitLayout& out, Level& level) {
    out.columnWidth = level.widths;
    out.columnX.resize(level.widths.size());
    double x = kMargin;
    for (std::size_t k = 0; k < level.widths.size(); ++k) {
        out.columnX[k] = x;
        x += level.widths[k] + kColumnGap;
    }
    out.width = x + kMargin - kColumnGap;
    for (Glyph& g : level.glyphs) {
        const std::size_t c0 = g.column, c1 = std::min<std::size_t>(level.widths.size() - 1,
                                                                    g.column + g.columnSpan - 1);
        if (g.kind == GlyphKind::Region) {
            const double inset = 0.04 * static_cast<double>(g.depth);
            g.bounds = {out.columnX[c0] - 0.12 + inset, out.rowY(g.rowMin) - 0.48 + inset,
                        out.columnX[c1] + out.columnWidth[c1] + 0.12 - inset,
                        out.rowY(g.rowMax) + 0.48 - inset};
        } else {
            const double w = detail::glyphWidth(g);
            const double cx = out.columnX[c0] + 0.5 * out.columnWidth[c0];
            g.bounds = {cx - 0.5 * w, out.rowY(g.rowMin) - kBoxHalf, cx + 0.5 * w,
                        out.rowY(g.rowMax) + kBoxHalf};
            g.parts = {g.bounds};
        }
    }
}

void assignTimedBounds(CircuitLayout& out, Level& level, const std::vector<NodeTiming>& times) {
    double shortest = 0.0;
    for (const auto& t : times)
        if (t.durationNs > 0.0 && (shortest == 0.0 || t.durationNs < shortest))
            shortest = t.durationNs;
    for (const auto& t : times)
        out.durationNs = std::max(out.durationNs, t.startNs + t.durationNs);
    out.nsPerUnit = shortest > 0.0 ? shortest : 1.0;
    if (out.durationNs / out.nsPerUnit > 20000.0)
        out.nsPerUnit = out.durationNs / 20000.0; // keep the axis drawable
    std::vector<double> tickEnd(
        out.rows.size(), -1.0); // zero-duration ticks on a wire are fanned out, never stacked
    for (Glyph& g : level.glyphs) {
        if (g.depth != 0 || g.topoIndex >= times.size())
            continue; // nested bodies have their own time axis
        const NodeTiming& t = times[g.topoIndex];
        g.startNs = t.startNs;
        g.durationNs = t.durationNs;
        double x0 = kMargin + t.startNs / out.nsPerUnit;
        const double w = t.durationNs / out.nsPerUnit;
        std::vector<std::uint32_t> rows = g.targetRows;
        rows.insert(rows.end(), g.controlRows.begin(), g.controlRows.end());
        if (g.kind == GlyphKind::Classical)
            rows = {g.rowMin};
        g.parts.clear();
        if (w <= 0.0) { // virtual gates, barriers: a tick above the wire, clear of the box band
            for (auto r : rows)
                x0 = std::max(x0, tickEnd[r] + 0.04);
            for (auto r : rows) {
                g.parts.push_back(
                    {x0, out.rowY(r) - 0.49, x0 + 0.16, out.rowY(r) - kBoxHalf - 0.02});
                tickEnd[r] = x0 + 0.16;
            }
        } else {
            for (auto r : rows)
                g.parts.push_back({x0, out.rowY(r) - kBoxHalf, x0 + w, out.rowY(r) + kBoxHalf});
        }
        if (g.parts.empty())
            continue;
        g.bounds = g.parts.front();
        for (const Rect& p : g.parts)
            g.bounds = g.bounds.unite(p);
        out.width = std::max(out.width, g.bounds.x1 + kMargin);
    }
    // Drop what has no place on the time axis (nested glyphs), keep regions as plain spans.
    std::erase_if(level.glyphs, [](const Glyph& g) { return g.depth != 0; });
    for (Glyph& g : level.glyphs)
        if (g.kind == GlyphKind::Region && g.startNs)
            g.bounds = {kMargin + *g.startNs / out.nsPerUnit, out.rowY(g.rowMin) - 0.48,
                        kMargin + (*g.startNs + g.durationNs.value_or(0.0)) / out.nsPerUnit,
                        out.rowY(g.rowMax) + 0.48};
}

} // namespace

Result<CircuitLayout> layoutCircuit(const ir::Circuit& circuit,
                                    const CircuitLayoutOptions& options) {
    CircuitLayout out;
    out.qubitRows = circuit.qubitCount();
    for (std::uint32_t q = 0; q < circuit.qubitCount(); ++q) {
        WireLabel w;
        w.name = circuit.wireName(ir::Wire{q});
        if (circuit.isPhysical()) {
            w.physical = q;
            for (std::size_t v = 0; v < options.layout.size(); ++v)
                if (options.layout[v] == q)
                    w.virtualQubit = static_cast<std::uint32_t>(v);
        }
        out.rows.push_back(std::move(w));
    }
    for (const auto& reg : circuit.bitRegisters()) {
        WireLabel w;
        w.name = reg.name;
        w.classical = true;
        w.bits = reg.size;
        out.rows.push_back(std::move(w));
    }
    const auto totalRows = static_cast<std::uint32_t>(out.rows.size());
    if (totalRows == 0)
        return out;
    out.height = static_cast<double>(totalRows);

    Level level = layoutLevel(circuit, circuit, 0, out.qubitRows, totalRows, 0);
    out.moments = level.moments;
    out.columns = level.widths.size();
    const auto times = options.timed ? scheduleTimes(circuit) : std::vector<NodeTiming>{};
    if (options.timed && times.empty())
        return fail(ErrorCode::InvalidArgument,
                    "timed layout needs a scheduled circuit (no meta.schedule record)");
    if (options.timed) {
        out.timed = true;
        assignTimedBounds(out, level, times);
    } else {
        if (level.widths.empty())
            level.widths.push_back(0.7);
        assignBounds(out, level);
    }
    for (Glyph& g : level.glyphs)
        if (options.markRoutingSwaps && g.kind == GlyphKind::Swap && g.controlRows.empty()) {
            g.routingSwap = true;
            ++out.routingSwaps;
        }
    out.glyphs = std::move(level.glyphs);
    return out;
}

const Glyph* CircuitLayout::glyphAt(double x, double y) const {
    const Glyph* best = nullptr;
    for (const Glyph& g : glyphs) {
        bool inside = false;
        if (g.kind == GlyphKind::Region)
            inside = g.bounds.contains(x, y);
        else
            for (const Rect& p : g.parts)
                inside = inside || p.contains(x, y);
        if (!inside)
            continue;
        // Prefer a gate over the region around it, and the deeper of two regions.
        if (!best || (best->kind == GlyphKind::Region &&
                      (g.kind != GlyphKind::Region || g.depth > best->depth)))
            best = &g;
    }
    return best;
}

std::optional<std::pair<std::size_t, std::size_t>> CircuitLayout::firstOverlap() const {
    for (std::size_t a = 0; a < glyphs.size(); ++a) {
        if (glyphs[a].kind == GlyphKind::Region)
            continue;
        for (std::size_t b = a + 1; b < glyphs.size(); ++b) {
            if (glyphs[b].kind == GlyphKind::Region)
                continue;
            for (const Rect& pa : glyphs[a].parts)
                for (const Rect& pb : glyphs[b].parts)
                    if (pa.overlaps(pb))
                        return std::pair{a, b};
        }
    }
    return std::nullopt;
}

} // namespace qlab::viz::layout
