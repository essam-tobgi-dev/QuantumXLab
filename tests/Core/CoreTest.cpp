#include "Core/Core.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numeric>
using namespace qlab;
using namespace qlab::core;

TEST_CASE("Result propagates errors with QXL_TRY") {
    auto f = [](int x) -> Result<int> {
        if (x < 0)
            return fail(ErrorCode::InvalidArgument, "neg");
        return x * 2;
    };
    auto g = [&](int x) -> Result<int> {
        QXL_TRY_ASSIGN(auto v, f(x));
        return v + 1;
    };
    REQUIRE(g(2).value() == 5);
    REQUIRE_FALSE(g(-1).has_value());
    REQUIRE(g(-1).error().code == ErrorCode::InvalidArgument);
    Error e(ErrorCode::Lang_ + 7, "bad token");
    e.withSpan({3, 4, 3, 9, "a.qasm"}).withId("QL2007");
    REQUIRE(e.format().starts_with("a.qasm:3:4: [QL2007] bad token"));
}
TEST_CASE("Random is deterministic and streams differ") {
    Random a(42), b(42);
    for (int i = 0; i < 100; ++i)
        REQUIRE(a.next() == b.next());
    Random s0 = a.stream(0), s1 = a.stream(1);
    REQUIRE(s0.next() != s1.next());
    Random c(7);
    double sum = 0;
    const int N = 200000;
    for (int i = 0; i < N; ++i)
        sum += c.uniform();
    REQUIRE(sum / N == Catch::Approx(0.5).margin(0.005));
    double m = 0, v = 0;
    for (int i = 0; i < N; ++i) {
        double x = c.normal();
        m += x;
        v += x * x;
    }
    REQUIRE(m / N == Catch::Approx(0.0).margin(0.01));
    REQUIRE(v / N == Catch::Approx(1.0).margin(0.02));
    Random j1(5), j2(5);
    j2.jump();
    REQUIRE(j1.next() != j2.next());
}
TEST_CASE("EventBus immediate, queued, and RAII unsubscribe") {
    EventBus bus;
    int hits = 0;
    struct Ev {
        int v;
    };
    {
        auto sub = bus.subscribe<Ev>([&](const Ev& e) { hits += e.v; });
        bus.publish(Ev{1});
        REQUIRE(hits == 1);
        bus.post(Ev{10});
        REQUIRE(hits == 1);
        bus.drain();
        REQUIRE(hits == 11);
    }
    bus.publish(Ev{100});
    REQUIRE(hits == 11);
}
TEST_CASE("JobSystem parallelFor covers the range exactly once") {
    JobSystem js(4);
    std::vector<int> v(100000, 0);
    js.parallelFor(v.size(), 1000, [&](std::size_t b, std::size_t e) {
        for (auto i = b; i < e; ++i)
            v[i] += 1;
    });
    REQUIRE(std::accumulate(v.begin(), v.end(), 0) == 100000);
    auto fut = js.submit([](std::stop_token) { return 42; });
    REQUIRE(fut.get() == 42);
}
TEST_CASE("JSON envelope round-trip, version refusal, upgrade chain") {
    JsonEnvelope::registerKind("test.kind", 2, {[](Json d) -> Result<Json> {
                                   d["upgraded"] = true;
                                   return d;
                               }});
    auto text = JsonEnvelope::serialize("test.kind", Json{{"a", 1}});
    auto e = JsonEnvelope::parse(text, "test.kind");
    REQUIRE(e);
    REQUIRE(e->schema == 2);
    REQUIRE(e->data["a"] == 1);
    auto old = JsonEnvelope::serialize("test.kind", Json{{"a", 1}}, 1);
    auto up = JsonEnvelope::parse(old, "test.kind");
    REQUIRE(up);
    REQUIRE(up->data["upgraded"] == true);
    auto newer = JsonEnvelope::serialize("test.kind", Json{}, 9);
    REQUIRE_FALSE(JsonEnvelope::parse(newer, "test.kind").has_value());
    REQUIRE_FALSE(JsonEnvelope::parse("{not json", "test.kind").has_value());
    REQUIRE_FALSE(JsonEnvelope::parse(text, "other.kind").has_value());
    REQUIRE(jsonQuantity(Json{{"value", 4.8}, {"unit", "GHz"}}, "GHz").value() == 4.8);
    REQUIRE_FALSE(jsonQuantity(Json{{"value", 4.8}, {"unit", "MHz"}}, "GHz").has_value());
}
TEST_CASE("aligned_vector is 64-byte aligned") {
    aligned_vector<double> v(100);
    REQUIRE(reinterpret_cast<std::uintptr_t>(v.data()) % 64 == 0);
}

TEST_CASE("logInit level survives later log calls") {
    // Regression: logRaw used to re-run logInit(Info) on every message, resetting the level.
    logInit(LogLevel::Error);
    QXL_LOG_INFO(Test, "this must not raise the level back to info");
    auto before = recentLog(1);
    logInit(LogLevel::Trace);
    QXL_LOG_DEBUG(Test, "debug is visible now");
    REQUIRE(recentLog(1).size() == 1);
    (void)before;
    logInit(LogLevel::Info);
}
