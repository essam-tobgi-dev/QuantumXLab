#pragma once
// Spec 14 §1, §11 — the compile façade, diagnostics formatting and compiled-program export.
#include "Compiler/Pass.hpp"
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include "Lang/Sema.hpp"
#include <stop_token>
#include <string>
#include <string_view>

namespace qlab::compiler {

// Options resolved against the program's pragmas: an option that is set wins, then
// `qlab.optimize`, `qlab.layout`, `qlab.routing`, `qlab.pulse_level`, `qlab.seed`, then the
// defaults of `CompileContext`. (`qlab.layout physical` is carried by the circuit itself.)
CompileContext resolveContext(const lang::Program& program, const CompileOptions& options,
                              const hw::Device* device, const hw::Calibration* calibration);

// Compiles an analysed program for a device: the fixed pipeline of spec 14 §1.1. For a pulse-level
// compile without `options.pulses` the device's `pulses.json` is loaded from `device.directory`
// and resolved against `calibration`. Errors carry the QL4xxx id and the span of the statement.
Result<CompiledProgram> compile(const lang::Program& program, const hw::Device& device,
                                const hw::Calibration& calibration,
                                const CompileOptions& options = {}, std::stop_token stop = {});
// Device-independent compile to {U, cx} (spec 14 §4.1): Build, Verify, Decompose, Optimize,
// Verify, Equivalence. A `dt` duration is QL4010.
Result<CompiledProgram> compile(const lang::Program& program, const CompileOptions& options = {},
                                std::stop_token stop = {});

// Parse + analyse + compile. A front-end error is returned as is (first error diagnostic of the
// program); `device` and `calibration` may both be null for a device-independent compile.
Result<CompiledProgram> compileSource(std::string_view text, std::string filename,
                                      const hw::Device* device, const hw::Calibration* calibration,
                                      const CompileOptions& options = {},
                                      std::stop_token stop = {});

// Spec 14 §11 format: `path:line:col: error[QL4030]: message`, then indented `note:` lines and a
// `help:` line with the fix.
std::string formatDiagnostic(const lang::Diagnostic& d);
std::string formatError(const Error& e);

// FNV-1a 64 of a program text (cache key of the incremental compiler, spec 14 §11).
std::uint64_t programHash(std::string_view text);

struct ExportOptions {
    bool explicitDelays = true; // idle gaps of the schedule become `delay[…] $q;` statements
    bool header = true;         // comment block: device, calibration time, layout, metrics
};
// Compiled OpenQASM 3 (spec 14 §11, 23 §5): physical operands `$k`, native gates only, `rz` kept as
// gates, delays explicit, `pragma qlab.layout physical`, `pragma qlab.mapping q0->$3, …` and
// `pragma qlab.schedule <policy> dt=<ns>ns`. The text re-imports and compiles to an equivalent
// circuit.
Result<std::string> exportQasm(const CompiledProgram& program, const ExportOptions& options = {});

} // namespace qlab::compiler
