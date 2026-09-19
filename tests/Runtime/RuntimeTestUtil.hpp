#pragma once
// Shared helpers of the Runtime tests: a session on a shipped device, compile + run in one call,
// and the example programs of `Assets/Programs`.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/Paths.hpp"
#include "Runtime/Runtime.hpp"
#include <fstream>
#include <map>
#include <memory>
#include <numbers>
#include <sstream>
#include <string>

namespace rtest {
using namespace qlab;
using namespace qlab::runtime;
inline constexpr double kPi = std::numbers::pi;

inline std::string readAsset(const std::string& relative) {
    const std::filesystem::path p = core::assetDir() / relative;
    std::ifstream f(p);
    INFO("reading " << p.string());
    REQUIRE(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline std::string source(std::string_view body) {
    return "OPENQASM 3.0;\ninclude \"stdgates.inc\";\n" + std::string(body);
}

// One session per device id, shared by every test in the binary: loading and compiling the shipped
// devices dominates the run time otherwise.
class Lab {
  public:
    explicit Lab(std::string_view deviceId) {
        auto st = session.selectDevice(deviceId);
        INFO("selectDevice " << deviceId << ": " << (st ? std::string() : st.error().format()));
        REQUIRE(st.has_value());
    }

    struct Job {
        ProgramId program{0};
        CompileHandle handle{0};
        compiler::CompileOptions options;
    };

    Job compile(std::string_view text, compiler::CompileOptions options = {}) {
        auto id = session.loadProgram(std::string(text), "test.qasm");
        INFO((id ? std::string() : id.error().format()));
        REQUIRE(id.has_value());
        auto handle = session.compile(*id, options);
        INFO((handle ? std::string() : handle.error().format()));
        REQUIRE(handle.has_value());
        auto st = session.waitCompile(*handle);
        INFO("compile: " << (st ? std::string() : st.error().format()));
        REQUIRE(st.has_value());
        return Job{*id, *handle, std::move(options)};
    }

    RunRequest request(const Job& job, RunOptions options) const {
        RunRequest r;
        r.program = session.compiled(job.handle);
        r.source = session.program(job.program);
        r.compileOptions = job.options;
        r.options = std::move(options);
        return r;
    }

    RunResult run(std::string_view text, RunOptions options = {},
                  compiler::CompileOptions copts = {}) {
        const Job job = compile(text, std::move(copts));
        auto out = session.runSync(request(job, std::move(options)));
        INFO("run: " << (out ? std::string() : out.error().format()));
        REQUIRE(out.has_value());
        return std::move(*out);
    }

    core::EventBus bus;
    runtime::Session session{&bus};
};

inline Lab& lab(const std::string& deviceId) {
    static std::map<std::string, std::unique_ptr<Lab>> cache;
    auto it = cache.find(deviceId);
    if (it == cache.end())
        it = cache.emplace(deviceId, std::make_unique<Lab>(deviceId)).first;
    return *it->second;
}

// Probability of a bitstring with its 1σ binomial error, for "within 5σ" assertions.
inline double sigma(double p, std::uint64_t n) {
    return n ? std::sqrt(std::max(p * (1.0 - p), 1e-12) / n) : 1.0;
}

inline const Expectation* find(const RunResult& r, std::string_view name) {
    return r.expectation(name);
}

} // namespace rtest
