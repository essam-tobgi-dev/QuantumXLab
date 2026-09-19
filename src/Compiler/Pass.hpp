#pragma once
// Spec 14 §1 — pass interface, the state passes share, the pass manager and the fixed pipeline.
#include "Compiler/Coupling.hpp"
#include "Compiler/Equivalence.hpp"
#include "Compiler/PulseLower.hpp"
#include "Compiler/Schedule.hpp"
#include "Compiler/Target.hpp"
#include "Compiler/Types.hpp"
#include "Lang/Sema.hpp"
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace qlab::compiler {

// State of one pipeline run. Passes read the resolved `CompileContext`, append diagnostics, and
// leave their products here for later passes and for the `CompileOutput`.
struct PassContext {
    explicit PassContext(const CompileContext& cc, std::stop_token st = {})
        : options(cc), stop(std::move(st)) {}

    const CompileContext& options;
    std::stop_token stop;
    std::vector<lang::Diagnostic>
        diagnostics; // of the running pass; the manager moves them into its PassResult

    Target target = Target::universal();          // native set (spec 14 §4.1)
    std::optional<CouplingGraph> coupling;        // built on first use by Layout / Route
    std::shared_ptr<const ir::Circuit> reference; // post-Build circuit, the equivalence reference
    Layout initialLayout, finalLayout;
    std::uint32_t swapCount = 0;
    ScheduleInfo schedule;
    std::optional<PulseSource> pulseSource; // program defcals + pulses.json, built by Schedule
    std::optional<PulseProgram> pulseProgram;
    std::optional<EquivalenceReport> equivalence;

    PassMetrics lastMetrics; // of the circuit as the running pass received it

    bool cancelled() const { return stop.stop_requested(); }
    // Metrics of `c` with the pipeline's swap count and critical path filled in.
    PassMetrics metrics(const ir::Circuit& c) const;
    // For a pass that did not add or remove nodes: the received metrics, swap count and critical
    // path refreshed, without walking the circuit again.
    PassMetrics unchanged() const;
    // The coupling graph of the device, built once.
    Result<const CouplingGraph*> graph();
};

class IPass {
  public:
    virtual ~IPass() = default;
    virtual std::string_view name() const = 0;
    // Transforms `c`; returns the metrics after the pass. QL4xxx findings that do not stop the
    // compile go to `ctx.diagnostics`; an error ends the pipeline.
    virtual Result<PassMetrics> run(ir::Circuit& c, PassContext& ctx) = 0;
};

// Result of a compile (spec 14 §1 `CompileOutput`; the façade calls it `CompiledProgram`).
struct CompileOutput {
    ir::Circuit circuit;                // native gates; physical and timed when a device was given
    ir::Circuit source;                 // the post-Build circuit the output is equivalent to
    ScheduleInfo timing;                // gate-level schedule (empty without a device)
    std::optional<PulseProgram> pulses; // pulse-level schedule (pulseLevel)
    Layout initialLayout, finalLayout;  // program qubit → physical qubit, before / after
    PassMetrics metrics;
    std::vector<lang::Diagnostic> diagnostics; // warnings and notes of every pass, in order
    std::vector<PassResult> trace; // per-pass metrics and wall time, for the Compiler panel
    std::optional<EquivalenceReport> equivalence; // pass 11, when it ran
    std::string deviceId, calibrationTimestamp;
    std::uint64_t programHash = 0; // FNV-1a of the source text, set by `compileSource`
};
using CompiledProgram = CompileOutput;

class PassManager {
  public:
    explicit PassManager(CompileContext context) : context_(std::move(context)) {}
    // The fixed pipeline of spec 14 §1.1 for this context:
    //   Verify, Decompose, [Optimize ≥O1], then with a device: Layout, Route, Decompose, [Optimize
    //   ≥O1], [VirtualZ ≥O1], Schedule, [PulseLower when pulseLevel]; finally Verify and
    //   Equivalence.
    // Pass 1 (Build) runs in the `lang::Program` overload of `run`.
    static PassManager standard(const CompileContext& context);

    void add(std::unique_ptr<IPass> pass) { passes_.push_back(std::move(pass)); }
    std::vector<std::string> passNames() const;
    const CompileContext& context() const { return context_; }

    Result<CompileOutput> run(ir::Circuit circuit, std::stop_token stop = {});
    Result<CompileOutput> run(const lang::Program& program, const ir::ParamMap& inputs = {},
                              std::stop_token stop = {});
    // Pass 1 alone (Build, spec 14 §2), recorded in the trace.
    Result<ir::Circuit> build(const lang::Program& program, const ir::ParamMap& inputs = {});
    // Runs only the passes [first, last) on a circuit that already went through the earlier ones
    // (incremental compilation caches the circuit after pass 4). `reference` is the post-Build
    // circuit.
    Result<CompileOutput> runRange(ir::Circuit circuit,
                                   std::shared_ptr<const ir::Circuit> reference, std::size_t first,
                                   std::size_t last, std::stop_token stop = {});
    // Index of the first device-dependent pass (Layout); passes before it depend on the program,
    // the optimization level and the native gate set only.
    std::size_t frontEnd() const;
    std::size_t size() const { return passes_.size(); }

    // Per-pass metrics of the last run, including the pass that failed (its diagnostics hold the
    // error). `run` starts a new trace; consecutive `runRange` calls extend it until cleared.
    const std::vector<PassResult>& trace() const { return trace_; }
    void clearTrace() { trace_.clear(); }
    void setTrace(std::vector<PassResult> earlier) {
        trace_ = std::move(earlier);
    } // resume after cached passes

  private:
    CompileContext context_;
    std::vector<std::unique_ptr<IPass>> passes_;
    std::vector<PassResult> trace_;
};

// The standard passes, for custom pipelines.
std::unique_ptr<IPass> makeVerifyPass();
std::unique_ptr<IPass> makeDecomposePass();
std::unique_ptr<IPass> makeOptimizePass();
std::unique_ptr<IPass> makeLayoutPass();
std::unique_ptr<IPass> makeRoutePass();
std::unique_ptr<IPass> makeVirtualZPass();
std::unique_ptr<IPass> makeSchedulePass();
std::unique_ptr<IPass> makePulseLowerPass();
std::unique_ptr<IPass> makeEquivalencePass();

} // namespace qlab::compiler
