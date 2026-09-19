// Spec 14 §9 — ASAP / ALAP scheduling on the device grid, critical path, idle intervals.
#include "Compiler/Schedule.hpp"
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/ScheduleImpl.hpp"
#include <algorithm>
#include <format>
#include <span>

namespace qlab::compiler {
namespace {
using detail::Durations;

std::string pretty(Picoseconds p) {
    const double ns = static_cast<double>(p.get()) * 1e-3;
    if (ns >= 1e6)
        return std::format("{:.3f} ms", ns * 1e-6);
    if (ns >= 1e3)
        return std::format("{:.3f} us", ns * 1e-3);
    return std::format("{:.3f} ns", ns);
}

// Lengths of the nodes of one level; bodies are scheduled recursively on their own time axis.
Result<std::vector<Picoseconds>> lengthsOf(std::vector<ir::Node>& nodes, Durations& d,
                                           const ScheduleOptions& o);

Result<ScheduleInfo> scheduleLevel(ir::Circuit& c, Durations& d, const ScheduleOptions& o) {
    std::vector<ir::Node> nodes = takeNodes(c);
    auto lengths = lengthsOf(nodes, d, o);
    if (!lengths) {
        setNodes(c, std::move(nodes)); // leave the circuit well formed
        return std::unexpected(lengths.error());
    }
    const std::size_t n = nodes.size();
    ScheduleInfo info;
    info.policy = o.policy;
    info.dt = d.dt;
    info.granule = d.granule;
    info.nodes.resize(n);

    // Ordering constraints: wires, and classical bits (any two nodes touching a bit stay ordered).
    // Flat storage: node i owns [offset[i], offset[i+1]) of each list.
    struct Spans {
        std::vector<std::uint32_t> offset{0}, items;
        std::span<const std::uint32_t> operator[](std::size_t i) const {
            return {items.data() + offset[i], offset[i + 1] - offset[i]};
        }
    } wires, bits;
    for (std::size_t i = 0; i < n; ++i) {
        if (const auto* g =
                std::get_if<ir::Gate>(&nodes[i])) { // the common case, without temporaries
            for (ir::Wire w : g->targets)
                if (w.index < c.qubitCount())
                    wires.items.push_back(w.index);
            for (ir::Wire w : g->controls)
                if (w.index < c.qubitCount())
                    wires.items.push_back(w.index);
        } else {
            for (ir::Wire w : ir::nodeWiresIn(nodes[i], c.qubitCount()))
                if (w.index < c.qubitCount())
                    wires.items.push_back(w.index);
            for (auto b : ir::nodeWrites(nodes[i]))
                bits.items.push_back(b.index);
            for (auto b : ir::nodeReads(nodes[i]))
                bits.items.push_back(b.index);
        }
        wires.offset.push_back(static_cast<std::uint32_t>(wires.items.size()));
        bits.offset.push_back(static_cast<std::uint32_t>(bits.items.size()));
    }
    // ASAP: a node starts when its last wire (or bit) becomes free. ALAP is ASAP on the reversed
    // order, mirrored about the critical path, which has the same length in both directions.
    const bool alap = o.policy == SchedulePolicy::Alap;
    std::vector<std::int64_t> wireFree(c.qubitCount(), 0), bitFree(c.clbitCount(), 0), begin(n, 0);
    std::int64_t total = 0;
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t i = alap ? n - 1 - k : k;
        std::int64_t t = 0;
        for (std::uint32_t w : wires[i])
            t = std::max(t, wireFree[w]);
        for (std::uint32_t b : bits[i])
            if (b < bitFree.size())
                t = std::max(t, bitFree[b]);
        begin[i] = t;
        const std::int64_t end = t + (*lengths)[i].get();
        for (std::uint32_t w : wires[i])
            wireFree[w] = end;
        for (std::uint32_t b : bits[i])
            if (b < bitFree.size())
                bitFree[b] = end;
        total = std::max(total, end);
    }
    for (std::size_t i = 0; i < n; ++i) {
        const std::int64_t len = (*lengths)[i].get();
        info.nodes[i] =
            TimedNode{Picoseconds{alap ? total - begin[i] - len : begin[i]}, Picoseconds{len}};
    }
    info.duration = Picoseconds{total};

    // Idle intervals (spec 14 §9): the gaps between consecutive nodes of a wire that carries an
    // operation. The wait before a wire's first node is not one: the qubit is still in |0⟩.
    std::vector<std::int64_t> lastEnd(c.qubitCount(), -1);
    std::vector<std::uint8_t> carries(c.qubitCount(), 0);
    for (std::size_t i = 0; i < n; ++i)
        if (ir::isQuantum(nodes[i]) || isControlNode(nodes[i]))
            for (ir::Wire w : ir::nodeWires(nodes[i]))
                if (w.index < carries.size())
                    carries[w.index] = 1;
    for (std::size_t i = 0; i < n; ++i) // same-wire nodes are time ordered in topological order
        for (std::uint32_t w : wires[i]) {
            if (!carries[w])
                continue;
            const std::int64_t s = info.nodes[i].start.get();
            if (lastEnd[w] >= 0 && s > lastEnd[w])
                info.idle[w].push_back({Picoseconds{lastEnd[w]}, Picoseconds{s}});
            lastEnd[w] = info.nodes[i].end().get();
        }

    core::Json starts = core::Json::array(), lens = core::Json::array(),
               idle = core::Json::object();
    for (const TimedNode& t : info.nodes) {
        starts.push_back(t.start.get());
        lens.push_back(t.duration.get());
    }
    for (const auto& [w, gaps] : info.idle) {
        core::Json list = core::Json::array();
        for (const IdleInterval& g : gaps)
            list.push_back({g.start.get(), g.end.get()});
        idle[std::format("{}", w)] = std::move(list);
    }
    setNodes(c, std::move(nodes));
    c.meta()["schedule"] = {{"policy", std::string(schedulePolicyName(o.policy))},
                            {"dt_ps", d.dt.get()},
                            {"granule_ps", d.granule.get()},
                            {"duration_ps", total},
                            {"start_ps", std::move(starts)},
                            {"length_ps", std::move(lens)},
                            {"idle", std::move(idle)}};
    return info;
}

Result<std::vector<Picoseconds>> lengthsOf(std::vector<ir::Node>& nodes, Durations& d,
                                           const ScheduleOptions& o) {
    std::vector<Picoseconds> out(nodes.size(), Picoseconds{0});
    auto bodyLength = [&](ir::Circuit& body) -> Result<Picoseconds> {
        QXL_TRY_ASSIGN(const ScheduleInfo inner, scheduleLevel(body, d, o));
        return inner.duration;
    };
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        ir::Node& n = nodes[i];
        if (auto* g = std::get_if<ir::Gate>(&n)) {
            QXL_TRY_ASSIGN(out[i], d.gate(*g));
            g->duration = out[i];
        } else if (auto* m = std::get_if<ir::Measure>(&n)) {
            QXL_TRY_ASSIGN(out[i], d.measure(m->qubit.index, m->span));
            m->duration = out[i];
        } else if (auto* r = std::get_if<ir::Reset>(&n)) {
            QXL_TRY_ASSIGN(out[i], d.reset(r->qubit.index, r->span));
            r->duration = out[i];
        } else if (auto* dl = std::get_if<ir::Delay>(&n)) {
            out[i] = d.ceilToGrid(dl->duration.resolve(d.dt));
            dl->duration =
                ir::Duration{out[i], 0}; // `dt` resolved, on the grid (export makes it explicit)
        } else if (auto* br = std::get_if<ir::Branch>(&n)) {
            QXL_TRY_ASSIGN(const Picoseconds a, bodyLength(*br->thenBody));
            Picoseconds b{0};
            if (br->elseBody.present()) {
                QXL_TRY_ASSIGN(b, bodyLength(*br->elseBody));
            }
            out[i] = d.latency + std::max(a, b); // the runtime pads the shorter arm (spec 14 §9)
        } else if (auto* lp = std::get_if<ir::Loop>(&n)) {
            QXL_TRY_ASSIGN(const Picoseconds a, bodyLength(*lp->body));
            out[i] = d.latency + a; // one pass; the iteration count is known per shot only
        } else if (auto* bx = std::get_if<ir::Box>(&n)) {
            QXL_TRY_ASSIGN(const Picoseconds a, bodyLength(*bx->body));
            out[i] = a;
            if (bx->duration) {
                const Picoseconds declared = d.ceilToGrid(bx->duration->resolve(d.dt));
                if (a > declared)
                    return fail(Error(err::BoxOverrun,
                                      std::format("the box body lasts {} but the box declares {}",
                                                  pretty(a), pretty(declared)))
                                    .withSpan(bx->span));
                out[i] = declared;
                bx->duration = ir::Duration{declared, 0};
            }
        }
    }
    return out;
}
} // namespace

Picoseconds ScheduleInfo::totalIdle() const {
    std::int64_t sum = 0;
    for (const auto& [w, gaps] : idle) {
        (void)w;
        for (const IdleInterval& g : gaps)
            sum += g.length().get();
    }
    return Picoseconds{sum};
}

Result<ScheduleInfo> schedule(ir::Circuit& c, const hw::Device& device,
                              const hw::Calibration& calibration, const ScheduleOptions& options,
                              const PulseSource* pulses) {
    Durations d(device, calibration, pulses);
    QXL_TRY_ASSIGN(ScheduleInfo info, scheduleLevel(c, d, options));
    const Picoseconds limit{
        static_cast<std::int64_t>(device.control.maxProgramDuration.si() * 1e12)};
    if (options.enforceMaxDuration && limit.get() > 0 && info.duration > limit)
        return fail(
            lang::Diagnostics::make("QL4090", SourceSpan{}, pretty(info.duration), pretty(limit))
                .error);
    return info;
}

Status requireResolvedDurations(const ir::Circuit& c) {
    for (ir::NodeId id : c.topologicalOrder()) {
        const ir::Node& n = c.node(id);
        const auto* dl = std::get_if<ir::Delay>(&n);
        const auto* bx = std::get_if<ir::Box>(&n);
        if ((dl && dl->duration.symbolic()) || (bx && bx->duration && bx->duration->symbolic())) {
            lang::Diagnostic d = lang::Diagnostics::make("QL4010", ir::nodeSpan(n));
            d.error.withNote("help: select a device ('pragma qlab.device <id>') or write the "
                             "duration in ns, us or ms");
            return fail(std::move(d.error));
        }
        Status st;
        forEachBody(n, [&](const ir::Circuit& body) {
            if (st)
                st = requireResolvedDurations(body);
        });
        QXL_TRY(st);
    }
    return {};
}

} // namespace qlab::compiler
