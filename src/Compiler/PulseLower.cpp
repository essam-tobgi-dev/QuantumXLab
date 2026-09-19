// Spec 14 §6 — assembling the pulse schedule of a scheduled circuit. Every node is placed at its
// gate-level start time, so the two time axes agree by construction.
#include "Compiler/PulseLower.hpp"
#include "Compiler/CircuitUtil.hpp"
#include <format>

namespace qlab::compiler {
namespace {

struct Lowering {
    const PulseSource& source;
    std::vector<pulse::Schedule> blocks; // already shifted to absolute time
    std::vector<PulseWindow> windows;
    std::vector<std::uint32_t> activeWires; // wires a delay without operands applies to

    pulse::ChannelId driveOf(std::uint32_t q) const {
        return source.device().technology == hw::Technology::IonChain ? pulse::ChannelId::raman(q)
                                                                      : pulse::ChannelId::drive(q);
    }

    // Global beams (ion detection, optical pumping, the global Raman beam) are one line shared by
    // every qubit: reading several ions at once plays ONE detection pulse. A pulse identical to one
    // already placed at the same time is therefore not repeated; a different or shifted one stays
    // and `verify` rejects the schedule (E_PULSE_OVERLAP).
    std::vector<pulse::Play> shared;

    Status place(pulse::Schedule block, Picoseconds at) {
        if (block.empty())
            return {};
        block.shift(at);
        bool repeated = false;
        auto isRepeat = [&](const pulse::Instruction& i) {
            const auto* p = std::get_if<pulse::Play>(&i);
            if (!p || pulse::channelKindArity(p->ch.kind) != 0)
                return false;
            for (const pulse::Play& s : shared)
                if (s.ch == p->ch && s.t0 == p->t0 && s.wf == p->wf)
                    return true;
            return false;
        };
        for (const auto& i : block.instructions())
            repeated = repeated || isRepeat(i);
        if (repeated) {
            pulse::Schedule kept(block.dt());
            for (const auto& f : block.frames())
                kept.addFrame(f);
            for (const auto& i : block.instructions())
                if (!isRepeat(i))
                    kept.insert(i, pulse::instructionStart(i));
            block = std::move(kept);
        }
        for (const auto& i : block.instructions())
            if (const auto* p = std::get_if<pulse::Play>(&i);
                p && pulse::channelKindArity(p->ch.kind) == 0)
                shared.push_back(*p);
        if (!block.empty())
            blocks.push_back(std::move(block));
        return {};
    }

    // Time slots of a nested body, from the schedule the Schedule pass stored in its metadata.
    static Result<std::vector<TimedNode>> slotsOf(const ir::Circuit& c) {
        const auto& meta = c.meta();
        if (!meta.contains("schedule") || !meta["schedule"].contains("start_ps") ||
            !meta["schedule"].contains("length_ps"))
            return fail(err::Unsupported,
                        "pulse lowering needs a scheduled circuit (run the Schedule pass first)");
        const auto& starts = meta["schedule"]["start_ps"];
        const auto& lengths = meta["schedule"]["length_ps"];
        if (!starts.is_array() || !lengths.is_array() || starts.size() != c.nodeCount() ||
            lengths.size() != c.nodeCount())
            return fail(err::Unsupported, "the schedule metadata does not match the circuit");
        std::vector<TimedNode> out;
        for (std::size_t i = 0; i < starts.size(); ++i) {
            if (!starts[i].is_number_integer() || !lengths[i].is_number_integer())
                return fail(err::Unsupported, "the schedule metadata of the circuit is malformed");
            out.push_back({Picoseconds{starts[i].get<std::int64_t>()},
                           Picoseconds{lengths[i].get<std::int64_t>()}});
        }
        return out;
    }

    // `parent` is the top-level index of the Branch whose arm is being lowered (kTopLevel outside).
    Status level(const ir::Circuit& c, const std::vector<TimedNode>& slots, Picoseconds offset,
                 bool topLevel, std::uint32_t parent, const std::string& condition) {
        std::uint32_t index = 0;
        for (ir::NodeId id : c.topologicalOrder()) {
            const ir::Node& n = c.node(id);
            const Picoseconds at = offset + slots[index].start, end = at + slots[index].duration;
            if (const auto* g = std::get_if<ir::Gate>(&n)) {
                if (g->width() > 0) {
                    QXL_TRY_ASSIGN(pulse::Schedule block, source.gateBlock(*g));
                    QXL_TRY(place(std::move(block), at));
                }
            } else if (const auto* m = std::get_if<ir::Measure>(&n)) {
                QXL_TRY_ASSIGN(pulse::Schedule block, source.measureBlock(m->qubit.index, m->span));
                QXL_TRY(place(std::move(block), at));
            } else if (const auto* r = std::get_if<ir::Reset>(&n)) {
                QXL_TRY_ASSIGN(pulse::Schedule block, source.resetBlock(r->qubit.index, r->span));
                QXL_TRY(place(std::move(block), at));
            } else if (const auto* d = std::get_if<ir::Delay>(
                           &n)) { // a frame delay on the drive line (spec 14 §6)
                const Picoseconds length = d->duration.resolve(source.dt());
                pulse::Schedule block(source.dt());
                std::vector<std::uint32_t> wires;
                for (ir::Wire w : d->wires)
                    wires.push_back(w.index);
                if (d->wires.empty())
                    wires = activeWires;
                for (std::uint32_t w : wires)
                    if (length.get() > 0)
                        block.insert(pulse::Delay{driveOf(w), {}, length}, Picoseconds{0});
                QXL_TRY(place(std::move(block), at));
            } else if (const auto* bx = std::get_if<ir::Box>(&n)) {
                QXL_TRY_ASSIGN(const auto inner, slotsOf(*bx->body));
                QXL_TRY(level(*bx->body, inner, at, false, parent, condition));
            } else if (const auto* br = std::get_if<ir::Branch>(&n)) {
                if ((*br->elseBody).nodeCount() > 0)
                    return fail(Error(err::Unsupported,
                                      "a pulse schedule has no branch: only 'if' without 'else' "
                                      "can be lowered (its arm is played "
                                      "conditionally by the backend)")
                                    .withSpan(br->span));
                if (!condition.empty())
                    return fail(Error(err::Unsupported,
                                      "nested conditions cannot be lowered to a pulse schedule")
                                    .withSpan(br->span));
                QXL_TRY_ASSIGN(const auto inner, slotsOf(*br->thenBody));
                const Picoseconds latency =
                    source.quantise(source.device().control.feedbackLatency.si(), false);
                QXL_TRY(level(*br->thenBody, inner, at + latency, false, topLevel ? index : parent,
                              br->cond.text()));
            } else if (std::holds_alternative<ir::Loop>(n)) {
                return fail(Error(err::Unsupported,
                                  "a run-time loop cannot be lowered to a fixed pulse schedule")
                                .withSpan(ir::nodeSpan(n)));
            }
            // A window is the node's scheduled slot, so the two time axes agree by construction.
            if (topLevel || !condition.empty())
                windows.push_back({index, topLevel ? PulseWindow::kTopLevel : parent, at, end,
                                   !condition.empty(), condition});
            ++index;
        }
        return {};
    }
};
} // namespace

Result<PulseProgram> lowerToPulses(const ir::Circuit& c, const ScheduleInfo& timing,
                                   const PulseSource& source) {
    if (!c.isPhysical())
        return fail(err::Unsupported, "pulse lowering takes a circuit on physical qubits");
    if (timing.nodes.size() != c.nodeCount())
        return fail(err::Unsupported, "pulse lowering needs the schedule of this circuit");
    Lowering low{source, {}, {}, usedWires(c), {}};
    QXL_TRY(low.level(c, timing.nodes, Picoseconds{0}, true, PulseWindow::kTopLevel, {}));

    // Blocks are merged pairwise (each merge re-sorts), which keeps the assembly O(n log² n).
    std::vector<pulse::Schedule> blocks = std::move(low.blocks);
    while (blocks.size() > 1) {
        std::vector<pulse::Schedule> next;
        for (std::size_t i = 0; i + 1 < blocks.size(); i += 2) {
            blocks[i].merge(blocks[i + 1]);
            next.push_back(std::move(blocks[i]));
        }
        if (blocks.size() % 2 == 1)
            next.push_back(std::move(blocks.back()));
        blocks = std::move(next);
    }
    PulseProgram out;
    out.schedule = pulse::Schedule(source.dt());
    for (const pulse::FrameDecl& f : source.programFrames())
        out.schedule.addFrame(f); // program frames take precedence
    if (!blocks.empty())
        out.schedule.merge(blocks.front());
    out.windows = std::move(low.windows);
    auto verified = out.schedule.verify(source.device(), &out.warnings);
    if (!verified)
        return fail(
            verified.error().withNote("while verifying the lowered pulse schedule (spec 10 §5)"));
    return out;
}

} // namespace qlab::compiler
