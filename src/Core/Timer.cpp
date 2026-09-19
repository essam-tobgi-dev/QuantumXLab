#include "Core/Timer.hpp"
namespace qlab::core {
Profiler& Profiler::global() {
    static Profiler p;
    return p;
}
void Profiler::record(std::string_view name, double ms) {
    std::lock_guard lk(mu_);
    auto& s = stats_[std::string(name)];
    s.totalMs += ms;
    s.lastMs = ms;
    s.maxMs = std::max(s.maxMs, ms);
    ++s.count;
}
std::unordered_map<std::string, ProfileStat> Profiler::snapshot() const {
    std::lock_guard lk(mu_);
    return stats_;
}
void Profiler::clear() {
    std::lock_guard lk(mu_);
    stats_.clear();
}
} // namespace qlab::core
