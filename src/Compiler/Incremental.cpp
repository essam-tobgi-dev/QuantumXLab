// Spec 14 §11 — debounced, cancellable compilation with a two-level cache.
#include "Compiler/Incremental.hpp"
#include "Core/Timer.hpp"
#include <algorithm>
#include <bit>

namespace qlab::compiler {

struct IncrementalCompiler::FrontEntry {
    std::shared_ptr<const ir::Circuit> reference;   // after Build
    ir::Circuit circuit;                            // after the last device-independent pass
    std::vector<PassResult> trace;
    std::vector<lang::Diagnostic> diagnostics;      // front-end warnings (QL3xxx) of the program
    // What the program's pragmas resolve to (they are part of the hashed text).
    OptimizeLevel level = OptimizeLevel::O1;
    LayoutPolicy layout = LayoutPolicy::NoiseAware;
    RoutingPolicy routing = RoutingPolicy::Sabre;
    bool pulseLevel = false;
    std::uint64_t seed = 0;
};
struct IncrementalCompiler::Snapshot {
    const hw::Device* device = nullptr;
    const hw::Calibration* calibration = nullptr;
    CompileOptions options;
};

namespace {
std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}
std::uint64_t mixText(std::uint64_t h, std::string_view s) { return mix(h, programHash(s)); }
template <class T> std::uint64_t opt(const std::optional<T>& o) { return o ? 1 + static_cast<std::uint64_t>(*o) : 0; }

template <class Cache> auto lookup(Cache& cache, std::uint64_t key) -> decltype(cache.front().second) {
    const auto it = std::find_if(cache.begin(), cache.end(), [&](const auto& e) { return e.first == key; });
    if (it == cache.end()) return nullptr;
    std::rotate(it, it + 1, cache.end());   // most recently used last
    return cache.back().second;
}
template <class Cache, class Value> void store(Cache& cache, std::uint64_t key, Value value, std::size_t capacity) {
    cache.emplace_back(key, std::move(value));
    if (cache.size() > std::max<std::size_t>(capacity, 1)) cache.erase(cache.begin());
}
} // namespace

IncrementalCompiler::IncrementalCompiler(core::JobSystem& jobs) : IncrementalCompiler(jobs, Settings{}) {}
IncrementalCompiler::IncrementalCompiler(core::JobSystem& jobs, Settings settings) : jobs_(jobs), settings_(settings) {}

IncrementalCompiler::~IncrementalCompiler() {
    std::shared_future<void> pending;
    {
        std::lock_guard lock(mutex_);
        stop_.request_stop();
        pending = job_;
    }
    wake_.notify_all();
    if (pending.valid()) pending.wait();
}

void IncrementalCompiler::setTarget(const hw::Device* device, const hw::Calibration* calibration, CompileOptions options) {
    std::lock_guard lock(mutex_);
    device_ = device;
    calibration_ = calibration;
    options_ = std::move(options);
}

void IncrementalCompiler::onUpdate(Callback callback) {
    std::lock_guard lock(mutex_);
    callback_ = std::move(callback);
}

std::uint64_t IncrementalCompiler::submit(std::string source, std::string filename) {
    std::shared_future<void> previous;
    std::uint64_t generation = 0;
    Snapshot snapshot;
    std::stop_token token;
    {
        std::lock_guard lock(mutex_);
        generation = ++generation_;
        stop_.request_stop();                  // the compile in flight, or its debounce wait, ends
        stop_ = std::stop_source();
        token = stop_.get_token();
        snapshot = Snapshot{device_, calibration_, options_};
        previous = job_;
    }
    wake_.notify_all();
    // Jobs run one after another: the superseded one returns at once, so this costs no latency, and
    // no job ever outlives the compiler unnoticed.
    auto task = [this, generation, source = std::move(source), filename = std::move(filename), snapshot, token,
                 previous](std::stop_token pool) {
        if (previous.valid()) previous.wait();
        work(generation, source, filename, snapshot, token, pool);
    };
    std::shared_future<void> next = jobs_.submit(std::move(task), core::JobPriority::Interactive).share();
    std::lock_guard lock(mutex_);
    if (generation == generation_) job_ = next;
    return generation;
}

void IncrementalCompiler::cancel() {
    {
        std::lock_guard lock(mutex_);
        ++generation_;
        stop_.request_stop();
        stop_ = std::stop_source();
    }
    wake_.notify_all();
}

std::shared_ptr<const IncrementalCompiler::Update> IncrementalCompiler::wait() {
    for (;;) {
        std::shared_future<void> pending;
        std::uint64_t seen = 0;
        {
            std::lock_guard lock(mutex_);
            pending = job_;
            seen = generation_;
        }
        if (pending.valid()) pending.wait();
        std::lock_guard lock(mutex_);
        if (seen == generation_) return latest_;   // nothing newer arrived while waiting
    }
}

std::shared_ptr<const IncrementalCompiler::Update> IncrementalCompiler::latest() const {
    std::lock_guard lock(mutex_);
    return latest_;
}

std::uint64_t IncrementalCompiler::cancelledCount() const {
    std::lock_guard lock(mutex_);
    return cancelled_;
}

void IncrementalCompiler::publish(std::shared_ptr<Update> update) {
    Callback callback;
    {
        std::lock_guard lock(mutex_);
        if (update->generation != generation_) { ++cancelled_; return; }   // superseded while compiling
        latest_ = update;
        callback = callback_;
    }
    if (callback) callback(*update);
}

void IncrementalCompiler::work(std::uint64_t generation, const std::string& source, const std::string& filename,
                               const Snapshot& target, std::stop_token superseded, std::stop_token pool) {
    std::stop_source combined;
    std::stop_callback onSupersede(superseded, [&] { combined.request_stop(); });
    std::stop_callback onShutdown(pool, [&] { combined.request_stop(); });
    const std::stop_token stop = combined.get_token();
    {   // Debounce (spec 14 §11): sleep until the deadline unless another edit arrives first.
        std::unique_lock lock(mutex_);
        const auto deadline = std::chrono::steady_clock::now() + settings_.debounce;
        wake_.wait_until(lock, stop, deadline, [] { return false; });
        if (stop.stop_requested()) { ++cancelled_; return; }
    }
    const core::Timer timer;
    const CompileOptions& o = target.options;
    std::uint64_t frontKey = mixText(programHash(source), filename);
    frontKey = mix(frontKey, opt(o.level.transform([](OptimizeLevel l) { return static_cast<int>(l); })));
    frontKey = mix(mix(frontKey, o.loopUnrollBound), o.kak ? 1 : 0);
    frontKey = mixText(mixText(frontKey, o.twoQubitBasis), target.device ? target.device->id : "{U, cx}");
    for (const auto& [name, value] : o.inputs) frontKey = mix(mixText(frontKey, name), std::bit_cast<std::uint64_t>(value));
    std::uint64_t fullKey = mix(frontKey, opt(o.layout.transform([](LayoutPolicy p) { return static_cast<int>(p); })));
    fullKey = mix(fullKey, opt(o.routing.transform([](RoutingPolicy p) { return static_cast<int>(p); })));
    fullKey = mix(mix(fullKey, opt(o.pulseLevel)), opt(o.seed));
    fullKey = mix(mix(fullKey, static_cast<std::uint64_t>(o.schedule)), o.verifyEquivalence ? 1 : 0);
    fullKey = mix(fullKey, reinterpret_cast<std::uintptr_t>(o.pulses));
    fullKey = mixText(fullKey, target.calibration ? target.calibration->timestamp : "");

    auto update = std::make_shared<Update>();
    update->generation = generation;
    update->hash = programHash(source);
    std::shared_ptr<const Update> cachedFull;
    std::shared_ptr<const FrontEntry> front;
    {
        std::lock_guard lock(mutex_);
        cachedFull = lookup(fullCache_, fullKey);
        if (!cachedFull) front = lookup(frontCache_, frontKey);
    }
    if (cachedFull) {
        *update = *cachedFull;
        update->generation = generation;
        update->fullyCached = update->frontEndCached = true;
        update->compileTime = std::chrono::microseconds(static_cast<std::int64_t>(timer.seconds() * 1e6));
        publish(std::move(update));
        return;
    }
    auto finish = [&](Result<CompiledProgram> result, const PassManager* pm) {
        if (!result && result.error().code == ErrorCode::Cancelled) {
            std::lock_guard lock(mutex_);
            ++cancelled_;
            return;
        }
        if (pm) update->trace = pm->trace();
        if (result) for (const auto& d : result->diagnostics) update->diagnostics.push_back(d);
        else {
            lang::Diagnostic d;
            d.error = result.error();
            update->diagnostics.push_back(std::move(d));
        }
        update->program = std::move(result);
        update->compileTime = std::chrono::microseconds(static_cast<std::int64_t>(timer.seconds() * 1e6));
        if (update->program) {
            update->program->programHash = update->hash;
            std::lock_guard lock(mutex_);
            store(fullCache_, fullKey, std::shared_ptr<const Update>(update), settings_.cacheEntries);
        }
        publish(update);
    };

    if (!front) {   // passes 1–4: parse, analyse, Build, Verify, Decompose, Optimize
        lang::Program program = lang::analyzeProgram(source, filename);
        for (const auto& d : program.diagnostics) update->diagnostics.push_back(d);
        if (!program.ok()) { finish(std::unexpected(program.errors().front()), nullptr); return; }
        PassManager pm = PassManager::standard(resolveContext(program, o, target.device, target.calibration));
        auto built = pm.build(program, o.inputs);
        if (!built) { finish(std::unexpected(built.error()), &pm); return; }
        auto reference = std::make_shared<const ir::Circuit>(*built);
        auto early = pm.runRange(std::move(*built), reference, 0, pm.frontEnd(), stop);
        if (!early) { finish(std::unexpected(early.error()), &pm); return; }
        auto entry = std::make_shared<FrontEntry>();
        entry->reference = reference;
        entry->circuit = std::move(early->circuit);
        entry->trace = pm.trace();
        entry->diagnostics = program.diagnostics;
        entry->level = pm.context().level;
        entry->layout = pm.context().layout;
        entry->routing = pm.context().routing;
        entry->pulseLevel = pm.context().pulseLevel;
        entry->seed = pm.context().seed;
        front = entry;
        std::lock_guard lock(mutex_);
        store(frontCache_, frontKey, front, settings_.cacheEntries);
    } else {
        update->frontEndCached = true;
        update->diagnostics = front->diagnostics;
    }
    // Passes 5–11 from the cached circuit; explicit options win over the program's pragmas.
    CompileContext ctx = CompileContext::from(o, target.device, target.calibration);
    ctx.level = front->level;
    if (!o.layout) ctx.layout = front->layout;
    if (!o.routing) ctx.routing = front->routing;
    if (!o.pulseLevel) ctx.pulseLevel = front->pulseLevel && target.device != nullptr;
    if (!o.seed) ctx.seed = front->seed;
    std::optional<pulse::PulseLibrary> library;
    if (ctx.pulseLevel && !ctx.pulses && target.device && !target.device->directory.empty())
        if (auto loaded = pulse::loadPulses(target.device->directory)) {
            auto resolved = target.calibration ? loaded->withCalibration(*target.calibration) : Result<pulse::PulseLibrary>(*loaded);
            library = resolved ? std::move(*resolved) : std::move(*loaded);
            ctx.pulses = &*library;
        }
    PassManager pm = PassManager::standard(ctx);
    pm.setTrace(front->trace);
    auto result = pm.runRange(front->circuit, front->reference, pm.frontEnd(), pm.size(), stop);
    finish(std::move(result), &pm);
}

} // namespace qlab::compiler
