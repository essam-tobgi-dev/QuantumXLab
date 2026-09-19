#include "Core/Random.hpp"
#include <bit>
namespace qlab::core {
namespace {
std::uint64_t splitmix64(std::uint64_t& x) {
    std::uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
} // namespace
void Random::reseed(std::uint64_t seed) {
    std::uint64_t x = seed;
    for (auto& v : s_)
        v = splitmix64(x);
    haveSpare_ = false;
}
std::uint64_t Random::next() {
    const std::uint64_t result = std::rotl(s_[1] * 5, 7) * 9;
    const std::uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = std::rotl(s_[3], 45);
    return result;
}
double Random::normal(double mean, double sigma) {
    if (haveSpare_) {
        haveSpare_ = false;
        return mean + sigma * spare_;
    }
    double u, v, s;
    do {
        u = uniform(-1.0, 1.0);
        v = uniform(-1.0, 1.0);
        s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);
    double m = std::sqrt(-2.0 * std::log(s) / s);
    spare_ = v * m;
    haveSpare_ = true;
    return mean + sigma * u * m;
}
int Random::poisson(double lambda) {
    if (lambda < 30.0) {
        double L = std::exp(-lambda), p = 1.0;
        int k = 0;
        do {
            ++k;
            p *= uniform();
        } while (p > L);
        return k - 1;
    }
    // Normal approximation with continuity correction for large lambda.
    int k = static_cast<int>(std::lround(normal(lambda, std::sqrt(lambda))));
    return k < 0 ? 0 : k;
}
void Random::jump() {
    static constexpr std::uint64_t J[] = {0x180ec6d33cfd0abaull, 0xd5a61266f0c9392cull,
                                          0xa9582618e03fc9aaull, 0x39abdc4529b1661cull};
    std::uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    for (auto j : J)
        for (int b = 0; b < 64; ++b) {
            if (j & (1ull << b)) {
                s0 ^= s_[0];
                s1 ^= s_[1];
                s2 ^= s_[2];
                s3 ^= s_[3];
            }
            next();
        }
    s_ = {s0, s1, s2, s3};
    haveSpare_ = false;
}
Random Random::stream(std::uint64_t index) const {
    // Derive a stream deterministically from the root state and the index (cheap, seeded).
    Random r;
    std::uint64_t x = s_[0] ^ (s_[1] * 0x9E3779B97F4A7C15ull) ^ (index + 1) * 0xD1B54A32D192ED03ull;
    r.reseed(x);
    r.next();
    r.next();
    return r;
}
} // namespace qlab::core
