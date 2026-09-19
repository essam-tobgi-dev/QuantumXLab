#pragma once
// Spec 23 §5 — program export. The source export is the editor text unchanged; the compiled export
// is `compiler::exportQasm` (which already emits `pragma qlab.layout physical`, `qlab.mapping` and
// `qlab.schedule`) plus the `pragma qlab.estimate` line of §5 when an estimate is at hand.
//
// The compiled text re-parses through `lang::parseProgram` and re-compiles to an equivalent
// circuit; `tests/Report/ProgramExportTest.cpp` asserts both.
#include "Compiler/Compile.hpp"
#include "Pulse/Schedule.hpp"
#include "Report/Types.hpp"
#include "Runtime/Estimate.hpp"
#include <filesystem>
#include <string>

namespace qlab::report {

struct ProgramExportOptions {
    bool explicitDelays = true;                  // idle gaps become `delay[…] $q;`
    bool header = true;                          // the `// compiled by quantumxlab …` block
    const runtime::Estimate* estimate = nullptr; // adds `pragma qlab.estimate wall_s=… fidelity=…`
};

// The `pragma qlab.estimate` line of spec 23 §5 (no trailing newline).
std::string estimatePragma(const runtime::Estimate& e);

Result<std::string> compiledQasm(const compiler::CompiledProgram& program,
                                 const ProgramExportOptions& options = {});
Status writeCompiledQasm(const std::filesystem::path& path,
                         const compiler::CompiledProgram& program,
                         const ProgramExportOptions& options = {});
// Spec 23 §5 source export: the editor text, unchanged.
Status writeProgramSource(const std::filesystem::path& path, std::string_view source);

// Spec 23 §5 (pulse level): the played envelope of every drive-like channel on the `dt` grid.
// Columns `t (s)`, then `<channel>_re (arb)` / `<channel>_im (arb)` per channel.
std::string scheduleSamplesCsv(const pulse::Schedule& schedule);
Status writeScheduleSamplesCsv(const std::filesystem::path& path, const pulse::Schedule& schedule);

} // namespace qlab::report
