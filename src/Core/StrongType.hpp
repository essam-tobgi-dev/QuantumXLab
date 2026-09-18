#pragma once
// Spec 04 §5 — strong identifier types.
#include <compare>
#include <cstdint>
#include <functional>

namespace qlab::core {

template <class T, class Tag> struct Strong {
    T value{};
    constexpr Strong() = default;
    constexpr explicit Strong(T v) : value(v) {}
    constexpr T get() const { return value; }
    constexpr auto operator<=>(const Strong&) const = default;
    constexpr Strong& operator++() { ++value; return *this; }
};

} // namespace qlab::core

template <class T, class Tag> struct std::hash<qlab::core::Strong<T, Tag>> {
    std::size_t operator()(const qlab::core::Strong<T, Tag>& s) const noexcept {
        return std::hash<T>{}(s.value);
    }
};

namespace qlab {
using QubitIndex = core::Strong<std::uint32_t, struct QubitIndexTag>;   // physical qubit
using ComponentId = core::Strong<std::uint32_t, struct ComponentIdTag>; // lab component / pick id
using Picoseconds = core::Strong<std::int64_t, struct PicosecondsTag>;  // integer time base (spec 10)
constexpr Picoseconds operator+(Picoseconds a, Picoseconds b) { return Picoseconds{a.value + b.value}; }
constexpr Picoseconds operator-(Picoseconds a, Picoseconds b) { return Picoseconds{a.value - b.value}; }
} // namespace qlab
