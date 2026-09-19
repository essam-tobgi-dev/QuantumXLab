#pragma once
// Spec 04 §5 — worker pool with cooperative cancellation and fork-join parallelFor.
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>
namespace qlab::core {
enum class JobPriority { Interactive, Batch };

class JobSystem {
  public:
    explicit JobSystem(unsigned workers = 0); // 0 = hardware_concurrency - 1 (min 1)
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    unsigned workerCount() const { return static_cast<unsigned>(threads_.size()); }

    // Submit a job; returns a future. The job receives a stop_token for cancellation.
    template <class F>
    auto submit(F&& f, JobPriority prio = JobPriority::Batch)
        -> std::future<std::invoke_result_t<F, std::stop_token>> {
        using R = std::invoke_result_t<F, std::stop_token>;
        auto task = std::make_shared<std::packaged_task<R(std::stop_token)>>(std::forward<F>(f));
        auto fut = task->get_future();
        enqueue([task](std::stop_token st) { (*task)(st); }, prio);
        return fut;
    }

    // Fork-join: run fn(begin,end) over [0,n) in chunks of `grain` on the pool, blocking the
    // caller. The caller thread participates. Spec 24 §3: grain default 2^14.
    void parallelFor(std::size_t n, std::size_t grain,
                     const std::function<void(std::size_t, std::size_t)>& fn);

    void requestStopAll() { globalStop_.request_stop(); }
    std::stop_token stopToken() const { return globalStop_.get_token(); }
    std::size_t pending() const;

    static JobSystem& global();

  private:
    struct Job {
        std::function<void(std::stop_token)> fn;
    };
    void enqueue(std::function<void(std::stop_token)> fn, JobPriority prio);
    void workerLoop(std::stop_token st);
    std::vector<std::jthread> threads_;
    std::deque<Job> interactive_, batch_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::stop_source globalStop_;
    bool shutdown_ = false;
};
} // namespace qlab::core
