// Spec 14 §11, 23 §5 — compiled-program export as OpenQASM 3 that re-imports to an equivalent
// circuit.
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/Compile.hpp"
#include "Core/Version.hpp"
#include <format>

namespace qlab::compiler {
namespace {

// The circuit with every idle gap of the schedule written out as a `delay` on that wire, so that
// an ASAP reschedule of the exported text reproduces the compiled timing (ALAP included).
ir::Circuit withExplicitDelays(const ir::Circuit& c, const ScheduleInfo& timing) {
    if (timing.nodes.size() != c.nodeCount())
        return c;
    ir::Circuit out = shellLike(c);
    std::vector<std::int64_t> lastEnd(c.qubitCount(),
                                      0); // from t = 0: the wait before a first gate is kept too
    std::size_t i = 0;
    for (ir::NodeId id : c.topologicalOrder()) {
        const ir::Node& n = c.node(id);
        const TimedNode& t = timing.nodes[i++];
        const bool operation =
            ir::isQuantum(n) || isControlNode(n); // directives never wait for a delay
        for (ir::Wire w : ir::nodeWires(n)) {
            if (w.index >= lastEnd.size() || !operation)
                continue;
            if (t.start.get() > lastEnd[w.index])
                out.add(ir::Delay{ir::Duration{Picoseconds{t.start.get() - lastEnd[w.index]}, 0},
                                  {w},
                                  ir::nodeSpan(n)});
        }
        for (ir::Wire w : ir::nodeWiresIn(n, c.qubitCount()))
            if (w.index < lastEnd.size())
                lastEnd[w.index] = t.end().get();
        out.add(n);
    }
    return out;
}

std::string nanoseconds(Picoseconds p) {
    return std::format("{}", static_cast<double>(p.get()) * 1e-3);
}
} // namespace

Result<std::string> exportQasm(const CompiledProgram& program, const ExportOptions& options) {
    const bool timed = !program.timing.empty();
    const ir::Circuit circuit = options.explicitDelays && timed
                                    ? withExplicitDelays(program.circuit, program.timing)
                                    : program.circuit;
    QXL_TRY_ASSIGN(std::string text, ir::toQasm(circuit));

    std::string mapping; // a program written on physical qubits has no mapping to report
    for (std::uint32_t v = 0; !program.source.isPhysical() && v < program.initialLayout.size(); ++v)
        mapping += std::format("{}{}->${}", v ? ", " : "", program.source.wireName(ir::Wire{v}),
                               program.initialLayout.v2p[v]);
    std::string header;
    if (options.header) {
        header += std::format("// compiled by quantumxlab {} for {}", core::version(),
                              program.deviceId.empty() ? "{U, cx}" : program.deviceId);
        if (!program.calibrationTimestamp.empty())
            header += std::format(" (calibration {})", program.calibrationTimestamp);
        header += "\n";
        if (!mapping.empty())
            header += std::format("// layout: {}; final: {}\n", program.initialLayout.text(),
                                  program.finalLayout.text());
        header += std::format(
            "// metrics: gates={} two_qubit={} depth={} t_count={} swaps={} duration_ns={}\n",
            program.metrics.gateCount, program.metrics.twoQubitCount, program.metrics.depth,
            program.metrics.tCount, program.metrics.swapCount,
            nanoseconds(program.metrics.estimatedDuration));
    }
    std::string pragmas;
    if (!mapping.empty() && program.circuit.isPhysical())
        pragmas += "pragma qlab.mapping " + mapping + "\n";
    if (timed)
        pragmas +=
            std::format("pragma qlab.schedule {} dt={}ns\n",
                        schedulePolicyName(program.timing.policy), nanoseconds(program.timing.dt));

    // toQasm starts with the version line and the stdgates include; the header comment follows the
    // first, the pragmas the second (spec 23 §5).
    const std::size_t firstLine = text.find('\n');
    const std::size_t secondLine =
        firstLine == std::string::npos ? std::string::npos : text.find('\n', firstLine + 1);
    if (secondLine == std::string::npos)
        return fail(ErrorCode::Internal, "unexpected OpenQASM preamble");
    text.insert(secondLine + 1, pragmas);
    text.insert(firstLine + 1, header);
    return text;
}

} // namespace qlab::compiler
