// Spec 14 §5 — optimizer driver (sweeps to a fixpoint) and the virtual-Z bookkeeping of §5.4.
#include "Compiler/Optimize.hpp"
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/OptimizeImpl.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::compiler {
namespace {

std::uint32_t gateCount(const std::vector<ir::Node>& nodes) {
    std::uint32_t n = 0;
    for (const auto& node : nodes)
        if (std::holds_alternative<ir::Gate>(node))
            ++n;
    return n;
}

Result<OptimizeStats> optimizeIn(ir::Circuit& c, Basis1q basis, const OptimizeOptions& o,
                                 std::stop_token stop) {
    std::vector<ir::Node> nodes = takeNodes(c);
    OptimizeStats stats;
    stats.gatesBefore = gateCount(nodes);
    Status bodies;
    for (ir::Node& n : nodes) {
        bodies = forEachBody(n, [&](ir::Circuit& body) -> Status {
            QXL_TRY(optimizeIn(body, basis, o, stop));
            return {};
        });
        if (!bodies)
            break;
    }
    bool cancelled = false;
    std::vector<std::uint8_t> dirty(c.qubitCount(), 1); // wires a pass still has to look at
    while (bodies && stats.iterations < o.maxIterations) {
        if (stop.stop_requested()) {
            cancelled = true;
            break;
        }
        ++stats.iterations;
        detail::WorkList w(std::move(nodes), c.qubitCount());
        detail::MergeRules rules{basis, o.commute, false, o.maxEntanglerAngle};
        std::vector<std::uint8_t> changedWires(c.qubitCount(), 0);
        auto stage =
            [&](auto&& sweep) { // runs a sweep on the wires that are dirty or changed so far
                for (std::size_t q = 0; q < dirty.size(); ++q)
                    w.dirty[q] = dirty[q] | changedWires[q];
                std::fill(w.touched.begin(), w.touched.end(), 0);
                const bool changed = sweep();
                for (std::size_t q = 0; q < dirty.size(); ++q)
                    changedWires[q] |= w.touched[q];
                return changed;
            };
        // In-place rewrites first and to their fixpoint, so that a resynthesis (which hides its
        // wire until the next pass) never consumes a cancellation that was available: cx · x · sx ·
        // cx.
        if (o.cancel) {
            bool again = stage([&] { return detail::sweepCancelMerge(w, rules); });
            while (again) {
                w.dirty = w.touched;
                std::fill(w.touched.begin(), w.touched.end(), 0);
                again = detail::sweepCancelMerge(w, rules);
                for (std::size_t q = 0; q < dirty.size(); ++q)
                    changedWires[q] |= w.touched[q];
            }
        }
        if (o.fuse)
            stage([&] { return detail::sweepFuse(w, basis); });
        if (o.cancel && o.fuse) {
            rules.resynthesize = true;
            stage([&] { return detail::sweepCancelMerge(w, rules); });
        }
        if (o.commute && o.fuse)
            stage([&] { return detail::sweepCommute(w); });
        nodes = w.flush();
        dirty = std::move(changedWires);
        if (std::find(dirty.begin(), dirty.end(), 1) == dirty.end())
            break; // a pass changed nothing: fixpoint
    }
    stats.gatesAfter = gateCount(nodes);
    setNodes(c, std::move(nodes)); // the circuit stays well formed on every exit path
    if (!bodies)
        return std::unexpected(bodies.error());
    if (cancelled)
        return fail(ErrorCode::Cancelled, "compile cancelled");
    return stats;
}
} // namespace

Result<OptimizeStats> optimize(ir::Circuit& c, Basis1q basis, const OptimizeOptions& options,
                               std::stop_token stop) {
    return optimizeIn(c, basis, options, stop);
}

// ---------------------------------------------------------------- §5.4 virtual Z
namespace {
bool isPlainRz(const ir::Gate& g) {
    return g.name == "rz" && g.controls.empty() && !g.opaque && !g.custom &&
           g.targets.size() == 1 && g.params.size() == 1;
}

// Marks every rz as a zero-duration frame change, nested bodies included.
std::uint32_t markFrameChanges(ir::Circuit& c) {
    std::vector<ir::Node> nodes = takeNodes(c);
    std::uint32_t count = 0;
    for (ir::Node& n : nodes) {
        if (auto* g = std::get_if<ir::Gate>(&n)) {
            if (!isPlainRz(*g))
                continue;
            g->duration = Picoseconds{0};
            ++count;
        } else {
            (void)forEachBody(n, [&](ir::Circuit& body) -> Status {
                count += markFrameChanges(body);
                return {};
            });
        }
    }
    setNodes(c, std::move(nodes));
    return count;
}
} // namespace

Result<VirtualZInfo> virtualZ(ir::Circuit& c) {
    VirtualZInfo info;
    info.frameChanges = markFrameChanges(c);
    info.finalPhase.assign(c.qubitCount(), 0.0);
    std::vector<std::uint32_t> atNode,
        onWire; // parallel arrays: one JSON value each, not one per gate
    std::vector<double> framePhase;
    std::uint32_t index = 0;
    const ir::Circuit& view = c; // const access: no cache invalidation while iterating
    for (ir::NodeId id : view.topologicalOrder()) {
        const ir::Node& n = view.node(id);
        if (const auto* g = std::get_if<ir::Gate>(&n)) {
            if (isPlainRz(*g)) {
                double& phase = info.finalPhase[g->targets[0].index];
                phase = wrapAngle(phase + (g->adjoint ? -g->params[0] : g->params[0]));
            } else {
                auto record =
                    [&](ir::Wire wire) { // the frame every pulse of this gate is played in (T07 §5)
                        if (wire.index >= info.finalPhase.size() ||
                            std::abs(info.finalPhase[wire.index]) <= kAngleEps)
                            return;
                        atNode.push_back(index);
                        onWire.push_back(wire.index);
                        framePhase.push_back(info.finalPhase[wire.index]);
                    };
                for (ir::Wire wire : g->targets)
                    record(wire);
                for (ir::Wire wire : g->controls)
                    record(wire);
            }
        }
        ++index;
    }
    core::Json finalPhases = core::Json::object();
    for (std::uint32_t q = 0; q < info.finalPhase.size(); ++q)
        if (std::abs(info.finalPhase[q]) > kAngleEps)
            finalPhases[std::format("{}", q)] = info.finalPhase[q];
    c.meta()["virtual_z"] = {{"frame_changes", info.frameChanges},
                             {"final_phase", std::move(finalPhases)},
                             {"phases",
                              {{"node", std::move(atNode)},
                               {"wire", std::move(onWire)},
                               {"phase", std::move(framePhase)}}}};
    return info;
}

} // namespace qlab::compiler
