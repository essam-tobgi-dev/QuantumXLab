#pragma once
// Internal to qlab::noise — non-throwing JSON field readers for the `qlab.noise/1` loader
// (spec 04 §8: every read returns Result and names the field path; spec 23 §1: unknown fields kept).
#include "Core/Json.hpp"
#include "Noise/Types.hpp"
#include "Numerics/Types.hpp"
#include <algorithm>
#include <charconv>
#include <format>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::noise::json {
using core::Json;

inline Status objectAt(const Json& j, const std::string& path) {
    if (!j.is_object()) return fail(err::BadJson, std::format("{}: expected an object", path));
    return {};
}
// The members of `o` whose keys are not in `known`, to be written back unchanged.
inline Json unknownFields(const Json& o, std::initializer_list<std::string_view> known) {
    Json extra = Json::object();
    for (auto it = o.begin(); it != o.end(); ++it)
        if (std::find(known.begin(), known.end(), std::string_view(it.key())) == known.end()) extra[it.key()] = it.value();
    return extra;
}
inline Result<double> number(const Json& o, const std::string& path, const char* key, double fallback) {
    auto it = o.find(key);
    if (it == o.end()) return fallback;
    if (!it->is_number()) return fail(err::BadJson, std::format("{}.{}: expected a number", path, key));
    return it->get<double>();
}
inline Result<std::string> text(const Json& o, const std::string& path, const char* key) {
    auto it = o.find(key);
    if (it == o.end()) return std::string{};
    if (!it->is_string()) return fail(err::BadJson, std::format("{}.{}: expected a string", path, key));
    return it->get<std::string>();
}
inline Result<std::uint32_t> indexFrom(std::string_view s, const std::string& path) {
    std::uint32_t v = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (s.empty() || ec != std::errc{} || ptr != s.data() + s.size())
        return fail(err::BadJson, std::format("{}: '{}' is not a qubit index", path, s));
    return v;
}
// "3" or "0-1": distinct qubit indices (spec 08 §5 keys).
inline Result<std::vector<std::uint32_t>> targetsFrom(std::string_view key, const std::string& path, std::size_t maxCount) {
    std::vector<std::uint32_t> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t dash = key.find('-', start);
        QXL_TRY_ASSIGN(const std::uint32_t q, indexFrom(key.substr(start, dash - start), path));
        if (std::find(out.begin(), out.end(), q) != out.end()) return fail(err::BadJson, std::format("{}: repeated qubit {}", path, q));
        out.push_back(q);
        if (dash == std::string_view::npos) break;
        start = dash + 1;
    }
    if (out.size() > maxCount) return fail(err::BadJson, std::format("{}: at most {} targets expected", path, maxCount));
    return out;
}
inline Result<std::vector<std::uint32_t>> indexList(const Json& j, const std::string& path) {
    if (!j.is_array()) return fail(err::BadJson, std::format("{}: expected an array of qubit indices", path));
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < j.size(); ++i) {
        if (!j[i].is_number_unsigned() || j[i].get<std::uint64_t>() > 0xFFFFFFFFull)
            return fail(err::BadJson, std::format("{}[{}]: expected a qubit index", path, i));
        out.push_back(static_cast<std::uint32_t>(j[i].get<std::uint64_t>()));
    }
    return out;
}
inline Result<num::RealMatrix> realMatrix(const Json& j, const std::string& path) {
    if (!j.is_array() || j.empty()) return fail(err::BadJson, std::format("{}: expected a square matrix", path));
    const std::size_t n = j.size();
    num::RealMatrix m(n, n);
    for (std::size_t r = 0; r < n; ++r) {
        if (!j[r].is_array() || j[r].size() != n) return fail(err::BadJson, std::format("{}[{}]: expected {} numbers", path, r, n));
        for (std::size_t c = 0; c < n; ++c) {
            if (!j[r][c].is_number()) return fail(err::BadJson, std::format("{}[{}][{}]: expected a number", path, r, c));
            m(r, c) = j[r][c].get<double>();
        }
    }
    return m;
}
inline Result<std::map<std::string, double>> numberMap(const Json& j, const std::string& path) {
    QXL_TRY(objectAt(j, path));
    std::map<std::string, double> out;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!it->is_number()) return fail(err::BadJson, std::format("{}.{}: expected a number", path, it.key()));
        out[it.key()] = it->get<double>();
    }
    return out;
}

} // namespace qlab::noise::json
