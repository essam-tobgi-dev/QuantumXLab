#include "Core/JobSystem.hpp"
#include <algorithm>
namespace qlab::core {
JobSystem::JobSystem(unsigned workers) {
    if (workers == 0) {
        unsigned hc = std::thread::hardware_concurrency();
        workers = hc > 1 ? hc - 1 : 1;
    }
    for (unsigned i = 0; i < workers; ++i)
        threads_.emplace_back([this](std::stop_token st) { workerLoop(st); });
}
JobSystem::~JobSystem() {
    { std::lock_guard lk(mu_); shutdown_ = true; }
    for (auto& t : threads_) t.request_stop();
    cv_.notify_all();
    // Join here, in the destructor body: `threads_` is declared first, so its members would
    // otherwise be destroyed (and joined) after mu_ and cv_ are already gone.
    for (auto& t : threads_)
        if (t.joinable()) t.join();
    threads_.clear();
}
void JobSystem::enqueue(std::function<void(std::stop_token)> fn, JobPriority prio) {
    { std::lock_guard lk(mu_);
      (prio == JobPriority::Interactive ? interactive_ : batch_).push_back({std::move(fn)}); }
    cv_.notify_one();
}
std::size_t JobSystem::pending() const { std::lock_guard lk(mu_); return interactive_.size() + batch_.size(); }
void JobSystem::workerLoop(std::stop_token st) {
    for (;;) {
        Job job;
        {
            std::unique_lock lk(mu_);
            cv_.wait(lk, [&] { return shutdown_ || st.stop_requested() || !interactive_.empty() || !batch_.empty(); });
            if ((shutdown_ || st.stop_requested()) && interactive_.empty() && batch_.empty()) return;
            if (!interactive_.empty()) { job = std::move(interactive_.front()); interactive_.pop_front(); }
            else { job = std::move(batch_.front()); batch_.pop_front(); }
        }
        job.fn(globalStop_.get_token());
    }
}
void JobSystem::parallelFor(std::size_t n, std::size_t grain,
                            const std::function<void(std::size_t, std::size_t)>& fn) {
    if (n == 0) return;
    if (grain == 0) grain = 1;
    std::size_t chunks = (n + grain - 1) / grain;
    std::size_t maxChunks = static_cast<std::size_t>(workerCount()) * 4 + 1;
    if (chunks > maxChunks) { chunks = maxChunks; grain = (n + chunks - 1) / chunks; chunks = (n + grain - 1) / grain; }
    if (chunks <= 1) { fn(0, n); return; }
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> done{0};
    std::size_t helpers = std::min<std::size_t>(chunks - 1, workerCount());
    std::mutex dmu; std::condition_variable dcv;
    auto work = [&] {
        for (;;) {
            std::size_t c = next.fetch_add(1);
            if (c >= chunks) break;
            std::size_t b = c * grain, e = std::min(n, b + grain);
            fn(b, e);
        }
        { std::lock_guard lk(dmu); ++done; }
        dcv.notify_all();
    };
    for (std::size_t i = 0; i < helpers; ++i) enqueue([work](std::stop_token) { work(); }, JobPriority::Interactive);
    work();
    std::unique_lock lk(dmu);
    dcv.wait(lk, [&] { return done.load() >= helpers + 1; });
}
JobSystem& JobSystem::global() { static JobSystem js; return js; }
} // namespace qlab::core
