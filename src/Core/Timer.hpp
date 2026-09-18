#pragma once
// Spec 04 §9 — wall-clock timer and named profiling scopes (spec 24 §7).
#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>
namespace qlab::core {
class Timer {
public:
    Timer() : t0_(clock::now()) {}
    double seconds() const { return std::chrono::duration<double>(clock::now() - t0_).count(); }
    double ms() const { return seconds() * 1e3; }
    void reset() { t0_ = clock::now(); }
private:
    using clock = std::chrono::steady_clock;
    clock::time_point t0_;
};
struct ProfileStat { double totalMs = 0; double lastMs = 0; double maxMs = 0; std::uint64_t count = 0; };
class Profiler {
public:
    static Profiler& global();
    void record(std::string_view name, double ms);
    std::unordered_map<std::string, ProfileStat> snapshot() const;
    void clear();
private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, ProfileStat> stats_;
};
class ProfileScope {
public:
    explicit ProfileScope(std::string_view name) : name_(name) {}
    ~ProfileScope() { Profiler::global().record(name_, t_.ms()); }
private:
    std::string name_; Timer t_;
};
#define QXL_PROFILE(name) ::qlab::core::ProfileScope _qxl_prof_##__LINE__(name)
} // namespace qlab::core
