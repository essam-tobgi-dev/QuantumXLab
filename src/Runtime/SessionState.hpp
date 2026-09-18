#pragma once
// Internal: the per-handle state the session keeps for an in-flight compile or run. Both are owned
// through unique_ptr, so a worker may hold a raw pointer while the map grows (spec 02 §6).
#include "Runtime/Session.hpp"

namespace qlab::runtime {

struct Session::CompileState {
    CompileHandle handle{0};
    ProgramId program{0};
    compiler::CompileOptions options;
    std::stop_source stop;
    std::future<void> future;
    std::atomic<bool> finished{false};
    std::optional<compiler::CompiledProgram> output;
    std::vector<lang::Diagnostic> diagnostics;
    Error error{ErrorCode::Ok, ""};
    bool ok = false;
};

struct Session::RunState {
    RunHandle handle{0};
    RunId id{0};
    ProgramId program{0};
    CompileHandle compileHandle{0};
    RunOptions options;
    std::stop_source stop;
    std::future<void> future;
    std::atomic<bool> finished{false};
    std::optional<RunResult> result;
    Error error{ErrorCode::Ok, ""};
    bool ok = false;
};

} // namespace qlab::runtime
