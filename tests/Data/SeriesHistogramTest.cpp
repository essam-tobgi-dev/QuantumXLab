#include "Data/Data.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
using namespace qlab;
using namespace qlab::data;
using Catch::Approx;

TEST_CASE("decimation keeps min/max envelope") {
    Series s;
    s.desc.id = "v";
    s.y.resize(1);
    for (int i = 0; i < 10000; ++i) {
        s.x.push_back(i * 1e-3);
        s.y[0].push_back(i % 100 == 50 ? 100.0 : (i % 100 == 0 ? -100.0 : 0.0));
    }
    Series d = decimate(s, 200);
    REQUIRE(d.size() <= 400);
    REQUIRE(d.size() >= 100);
    double mn = 1e9, mx = -1e9;
    for (double v : d.y[0]) {
        mn = std::min(mn, v);
        mx = std::max(mx, v);
    }
    REQUIRE(mn == -100.0);
    REQUIRE(mx == 100.0);
    for (std::size_t i = 1; i < d.size(); ++i)
        REQUIRE(d.x[i] >= d.x[i - 1]);
    Series small = decimate(s, 20000);
    REQUIRE(small.size() == s.size());
}

TEST_CASE("recorder channels, derived, markers") {
    Recorder r;
    auto a = r.add({"a", "V"});
    auto b = r.addDerived({"a2", "V^2"}, a, [](double, std::span<const double> y) {
        return std::vector<double>{y[0] * y[0]};
    });
    REQUIRE(r.push(a, 0.0, 2.0));
    REQUIRE(r.push(a, 1.0, 3.0));
    r.mark(a, 0.5, "event");
    auto va = r.view(a), vb = r.view(b);
    REQUIRE(va.size() == 2);
    REQUIRE(vb.size() == 2);
    REQUIRE(vb.y[0][1] == 9.0);
    REQUIRE(va.markers.size() == 1);
    REQUIRE(r.find("a2").has_value());
}

TEST_CASE("histogram labels are little-endian and marginals select qubit bits") {
    Histogram h(2);
    h.add("01", 30); // q1=0, q0=1
    h.add("11", 70);
    REQUIRE(h.total() == 100);
    REQUIRE(Histogram::labelFromIndex(1, 2) == "01");
    REQUIRE(Histogram::indexFromLabel("10") == 2);
    std::size_t q0[] = {0};
    auto m0 = h.marginal(q0);
    REQUIRE(m0.count("1") == 100);
    std::size_t q1[] = {1};
    auto m1 = h.marginal(q1);
    REQUIRE(m1.count("0") == 30);
    REQUIRE(m1.count("1") == 70);
    auto top = h.topK(1);
    REQUIRE(top[0].first == "11");
    REQUIRE(h.probability("01") == Approx(0.3));
}

TEST_CASE("Wilson interval known values") {
    auto w = wilson(50, 100, 1.0);
    REQUIRE(w.center == Approx(0.5).margin(1e-12));
    REQUIRE(w.lo == Approx(0.45025).margin(2e-4));
    REQUIRE(w.hi == Approx(0.54975).margin(2e-4));
    auto z = wilson(0, 10, 1.0);
    REQUIRE(z.lo == 0.0);
    REQUIRE(z.hi > 0.0);
    REQUIRE(binomialSigma(0, 100) > 0.0);
}

TEST_CASE("Histogram1D and 2D binning") {
    std::vector<double> v;
    for (int i = 0; i < 1000; ++i)
        v.push_back(i / 1000.0);
    auto h = Histogram1D::build(v, 10);
    REQUIRE(h.counts.size() == 10);
    REQUIRE(h.total == 1000);
    for (auto c : h.counts)
        REQUIRE(c == 100);
    auto h2 = Histogram2D::build(v, v, 4, 4);
    REQUIRE(h2.total == 1000);
    REQUIRE(h2.at(0, 0) == 250);
}
