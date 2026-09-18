#pragma once
// Spec 04 §6 — xoshiro256** seeded via SplitMix64; per-shot streams by jump (T11 §9).
#include <array>
#include <cstdint>
#include <limits>
#include <cmath>
namespace qlab::core {
class Random {
public:
    using result_type = std::uint64_t;
    explicit Random(std::uint64_t seed = 0x9E3779B97F4A7C15ull) { reseed(seed); }
    void reseed(std::uint64_t seed);
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return std::numeric_limits<std::uint64_t>::max(); }
    result_type operator()() { return next(); }
    std::uint64_t next();
    // Uniform in [0,1).
    double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
    double uniform(double a, double b) { return a + (b - a) * uniform(); }
    double normal(double mean = 0.0, double sigma = 1.0);
    std::uint64_t uniformInt(std::uint64_t n) { return n == 0 ? 0 : next() % n; }
    bool bernoulli(double p) { return uniform() < p; }
    int poisson(double lambda);
    // Jump 2^128 outputs ahead: independent stream for a shot/purpose (spec 04 §6).
    void jump();
    Random stream(std::uint64_t index) const;
    std::array<std::uint64_t, 4> state() const { return s_; }
private:
    std::array<std::uint64_t, 4> s_{};
    bool haveSpare_ = false;
    double spare_ = 0.0;
};
} // namespace qlab::core
