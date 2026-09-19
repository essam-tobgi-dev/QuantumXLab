#pragma once
// Spec 14 §11 — incremental compilation for the editor: a compile starts 150 ms after the last
// edit on a `core::JobSystem` worker with a `std::stop_token`; a new edit cancels the job in
// flight. Passes 1–4 are cached by (program hash, inputs, level, native gate set), the whole result
// by that key plus (device id, calibration timestamp, options).
#include "Compiler/Compile.hpp"
#include "Core/JobSystem.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace qlab::compiler {

class IncrementalCompiler {
  public:
    struct Settings {
        std::chrono::milliseconds debounce{150};
        std::size_t cacheEntries = 8; // per cache level, least recently used evicted
    };
    // What a finished compile publishes (the pass trace is inside the program, or in `trace` when
    // the compile failed).
    struct Update {
        std::uint64_t generation = 0; // value returned by the `submit` that produced it
        std::uint64_t hash = 0;       // `programHash` of the text
        Result<CompiledProgram> program =
            std::unexpected(Error(ErrorCode::Unknown, "not compiled"));
        std::vector<lang::Diagnostic> diagnostics; // front end and compiler, errors included
        std::vector<PassResult> trace;
        bool frontEndCached = false;              // passes 1–4 came from the cache
        bool fullyCached = false;                 // the whole result came from the cache
        std::chrono::microseconds compileTime{0}; // excludes the debounce wait
    };
    using Callback = std::function<void(const Update&)>;

    explicit IncrementalCompiler(core::JobSystem& jobs);
    IncrementalCompiler(core::JobSystem& jobs, Settings settings);
    ~IncrementalCompiler(); // cancels the job in flight and waits for it
    IncrementalCompiler(const IncrementalCompiler&) = delete;
    IncrementalCompiler& operator=(const IncrementalCompiler&) = delete;

    // Device, calibration (both may be null: device-independent compile) and options of the next
    // compiles. The pointees must outlive this object.
    void setTarget(const hw::Device* device, const hw::Calibration* calibration,
                   CompileOptions options = {});
    // Called on the worker thread for every published update.
    void onUpdate(Callback callback);

    // An edit: cancels the compile in flight and schedules one `debounce` after this call.
    // Returns the generation of the submission.
    std::uint64_t submit(std::string source, std::string filename = "");
    void cancel();
    // Blocks until the newest submission was published or cancelled; returns the last update.
    std::shared_ptr<const Update> wait();
    std::shared_ptr<const Update> latest() const;
    std::uint64_t cancelledCount() const; // submissions superseded before or while compiling

  private:
    struct FrontEntry;
    struct Snapshot;
    void work(std::uint64_t generation, const std::string& source, const std::string& filename,
              const Snapshot& target, std::stop_token superseded, std::stop_token pool);
    void publish(std::shared_ptr<Update> update);

    core::JobSystem& jobs_;
    Settings settings_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::uint64_t generation_ = 0, cancelled_ = 0;
    std::stop_source stop_;
    std::shared_future<void> job_;
    const hw::Device* device_ = nullptr;
    const hw::Calibration* calibration_ = nullptr;
    CompileOptions options_;
    Callback callback_;
    std::shared_ptr<const Update> latest_;
    std::vector<std::pair<std::uint64_t, std::shared_ptr<const FrontEntry>>>
        frontCache_; // most recent last
    std::vector<std::pair<std::uint64_t, std::shared_ptr<const Update>>> fullCache_;
};

} // namespace qlab::compiler
