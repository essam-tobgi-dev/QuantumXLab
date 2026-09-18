// Spec 03 §3 — `quantumxlab --run`: parse, compile, run, print (see Headless.hpp).
#include "App/Headless.hpp"
#include "App/RunProgram.hpp"
#include "Compiler/Compile.hpp"
#include "Core/JobSystem.hpp"
#include "Report/Report.hpp"
#include "Runtime/Runtime.hpp"
#include <format>
#include <ostream>

namespace qlab::app {
namespace {

std::string sigfig(double v, int digits = 4) { return std::format("{:.{}g}", v, digits); }

void printCounts(const runtime::RunResult& r, std::ostream& out) {
    const std::uint64_t total = r.counts.total();
    if (total == 0) {
        out << "  (no classical bits were written)\n";
        return;
    }
    for (const auto& [label, count] : r.counts.topK(16)) {
        const data::Interval ci = r.counts.interval(label, 1.0);
        out << std::format("  {:<12} {:>9}  {:.4f}  [{:.4f}, {:.4f}]\n", label, count,
                           static_cast<double>(count) / static_cast<double>(total), ci.lo, ci.hi);
    }
    if (r.counts.distinct() > 16)
        out << std::format("  … {} further outcomes\n", r.counts.distinct() - 16);
}

void printEstimate(const runtime::Estimate& e, std::ostream& out) {
    out << "estimate (class Model — what this program would cost on the physical device)\n";
    out << std::format("  wall time      {} s   [{} … {}]\n", sigfig(e.wallTime.valueS), sigfig(e.wallTime.minS),
                       sigfig(e.wallTime.maxS));
    out << std::format("  per shot       {} s   (reset {} + circuit {} + readout {})\n",
                       sigfig(e.wallTime.perShotS), sigfig(e.wallTime.resetS), sigfig(e.wallTime.circuitS),
                       sigfig(e.wallTime.readoutS));
    out << std::format("  fidelity       {}   [{} … {}]\n", sigfig(e.fidelity.fast), sigfig(e.fidelity.low),
                       sigfig(e.fidelity.high));
    if (e.fidelity.simulated)
        out << std::format("  simulated F_c  {}   (Hellinger {})\n", sigfig(e.fidelity.simulated->classical),
                           sigfig(e.fidelity.simulated->hellinger));
    out << std::format("  resources      {} qubits, depth {}, {} two-qubit gates, {} T\n", e.resources.qubits,
                       e.resources.depth, e.resources.twoQubit, e.resources.tCount);
    if (e.qec)
        out << std::format("  surface code   distance {}, {} physical qubits\n", e.qec->distance,
                           e.qec->physicalQubits);
}

} // namespace

int runHeadless(const Options& options, std::ostream& out, std::ostream& err) {
    core::EventBus bus;
    runtime::Session session(&bus, &core::JobSystem::global());
    if (auto st = session.selectDevice(options.device); !st) {
        err << "quantumxlab: " << st.error().format() << "\n";
        return 1;
    }
    session.selectBackend(options.backend);

    const Result<ProgramRun> run = compileAndRunFile(session, options.program, options);
    if (!run) {
        err << "quantumxlab: " << run.error().format() << "\n";
        return 1;
    }
    const runtime::RunResult& r = run->result;

    if (options.json) {
        report::ResultExportOptions ro;
        ro.writeMemory = false;   // the memory blob is a file, not part of a printed document
        ro.compileTime = run->compileTime;
        out << report::serializeRunResult(r, ro) << "\n";
        return r.partial ? 1 : 0;
    }

    out << versionText() << "\n";
    out << std::format("program    {}\n", options.program.string());
    out << std::format("device     {}   calibration {}\n", r.device, r.calibrationTimestamp);
    out << std::format("backend    {}   {}\n", qsim::kindName(r.backend), r.backendReason);
    out << std::format("shots      {}   seed {}   class {}\n", r.options.shots, r.seed,
                       data::fidelityName(r.backendClass));
    out << std::format("sim time   {:.1f} ms\n",
                       static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(r.wallTime).count()) /
                           1000.0);
    if (r.partial) out << "run was cancelled: the result is partial\n";
    out << "counts (68 % Wilson interval)\n";
    printCounts(r, out);
    if (!r.expectations.empty()) {
        out << "expectations\n";
        for (const runtime::Expectation& e : r.expectations)
            out << std::format("  {:<10} {:>10}  ± {}\n", e.observable, sigfig(e.value), sigfig(e.stderr_, 2));
    }
    printEstimate(r.estimate, out);
    for (const lang::Diagnostic& d : run->diagnostics)
        if (d.severity != lang::Severity::Info) err << compiler::formatDiagnostic(d) << "\n";
    return r.partial ? 1 : 0;
}

} // namespace qlab::app
