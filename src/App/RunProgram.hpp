#pragma once
// Spec 15 §1–§3 — compiling and running one program, with the program's `pragma qlab.*` resolved
// against whatever the caller pinned. Shared by the GUI's Run button, `--run` and `--selftest`, so
// all three reach the same result for the same inputs.
#include "App/Options.hpp"
#include "Runtime/Runtime.hpp"
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace qlab::app {

struct ProgramRun {
    runtime::RunResult result;
    std::vector<lang::Diagnostic> diagnostics;   // front end + compiler, in order
    std::chrono::milliseconds compileTime{0};
    std::chrono::milliseconds runTime{0};
    runtime::ProgramId program{0};
    runtime::CompileHandle compile{0};
};

// Run options for `program` (spec 15 §1): the command line wins where it pinned a value, then the
// program's pragmas, then the defaults. `shots`/`seed`/`backend` follow `Options::*Given`.
runtime::RunOptions runOptionsFor(const lang::Program& program, const Options& o);
// The compile options the same resolution produces (`resolveContext` then merges the pragmas).
compiler::CompileOptions compileOptionsFor(const Options& o);

// Loads, compiles and runs `source` on `session` (which must already have a device). A compile
// error is returned as an `Error` carrying the first error diagnostic; warnings ride along in
// `ProgramRun::diagnostics`.
Result<ProgramRun> compileAndRun(runtime::Session& session, std::string source, std::filesystem::path origin,
                                 const Options& o);
// The same from a file.
Result<ProgramRun> compileAndRunFile(runtime::Session& session, const std::filesystem::path& file, const Options& o);

} // namespace qlab::app
