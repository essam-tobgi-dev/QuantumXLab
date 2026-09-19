#pragma once
// Spec 22 §1 — channels, recorder, series views, decimation; §2 Trace2D record.
#include "Core/Error.hpp"
#include "Core/StrongType.hpp"
#include "Data/Fidelity.hpp"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qlab::data {

using ChannelId = core::Strong<std::uint32_t, struct ChannelIdTag>;

struct ChannelDesc {
    std::string id;          // "t1.p1", "vna.s21.mag", "cryo.mxc.T"
    std::string unit;        // display unit string, "us", "dB", "mK" (Units catalog resolves it)
    std::string xUnit = "s"; // unit of the x axis (time or sweep value)
    FidelityClass cls = FidelityClass::Numerical;
    std::size_t arity = 1; // 1 scalar, 2 complex/IQ, n vector
};

struct Marker {
    double x;
    std::string label;
};

// Immutable snapshot of a channel's samples. y is column-major per component: y[k][i].
struct Series {
    ChannelDesc desc;
    std::vector<double> x;
    std::vector<std::vector<double>> y; // size arity, each of size x.size()
    std::vector<Marker> markers;
    std::size_t size() const { return x.size(); }
    bool empty() const { return x.empty(); }
};

// Common plot/instrument data record (spec 12 §2, 22 §2).
struct Trace2D {
    std::string name;
    std::string xUnit, yUnit;
    std::vector<double> x, y;
    std::optional<std::vector<double>> yIm;   // imaginary part for complex traces
    std::optional<std::vector<double>> sigma; // 1σ per sample (Statistical)
    std::vector<Marker> markers;
    FidelityClass cls = FidelityClass::Numerical;
    double timestampS = 0.0;
    std::size_t size() const { return x.size(); }
};

// Min/max envelope decimation (spec 22 §1): `maxPoints` buckets over [x0,x1]; each non-empty
// bucket contributes its min and max sample in x order. Returns exact samples when the range
// holds <= maxPoints samples. Input x must be sorted ascending.
Series decimate(const Series& s, double x0, double x1, std::size_t maxPoints);
Series decimate(const Series& s, std::size_t maxPoints);
std::vector<std::pair<double, double>> decimate(std::span<const double> x,
                                                std::span<const double> y, double x0, double x1,
                                                std::size_t maxPoints);

class Recorder {
  public:
    ChannelId add(ChannelDesc desc);
    std::optional<ChannelId> find(std::string_view id) const;
    // Derived channel: computed from other channels on each push of `source`.
    ChannelId
    addDerived(ChannelDesc desc, ChannelId source,
               std::function<std::vector<double>(double x, std::span<const double> y)> fn);
    Status push(ChannelId id, double x, std::span<const double> y);
    Status push(ChannelId id, double x, double y) {
        return push(id, x, std::span<const double>(&y, 1));
    }
    void mark(ChannelId id, double x, std::string label);
    void reserve(ChannelId id, std::size_t n);
    Series view(ChannelId id) const;
    Series decimated(ChannelId id, double x0, double x1, std::size_t maxPoints) const;
    std::vector<ChannelDesc> channels() const;
    void freeze() { frozen_ = true; }
    bool frozen() const { return frozen_; }
    std::size_t size(ChannelId id) const;
    void clear();

  private:
    struct Entry {
        ChannelDesc desc;
        std::vector<double> x;
        std::vector<std::vector<double>> y;
        std::vector<Marker> markers;
        std::vector<std::pair<ChannelId,
                              std::function<std::vector<double>(double, std::span<const double>)>>>
            derived;
    };
    mutable std::mutex mu_;
    std::vector<Entry> entries_;
    std::map<std::string, ChannelId, std::less<>> byId_;
    bool frozen_ = false;
};

} // namespace qlab::data
