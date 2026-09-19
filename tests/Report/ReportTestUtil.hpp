#pragma once
// Shared helpers of the Report tests: a scratch directory that cleans itself up, a session on a
// shipped device, and one compiled + executed program shared by every test in the binary.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/Paths.hpp"
#include "Core/Version.hpp"
#include "Report/Report.hpp"
#include "Runtime/Runtime.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace rtest {
using namespace qlab;

// A unique directory under the system temp path, removed when the object dies.
class Sandbox {
  public:
    explicit Sandbox(std::string_view name) {
        static std::atomic<unsigned> counter{0};
        dir_ = std::filesystem::temp_directory_path() /
               ("qxl_report_" + std::string(name) + "_" + std::to_string(counter.fetch_add(1)));
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
        std::filesystem::create_directories(dir_, ec);
    }
    ~Sandbox() {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;
    const std::filesystem::path& path() const { return dir_; }
    std::filesystem::path operator/(std::string_view leaf) const { return dir_ / leaf; }

  private:
    std::filesystem::path dir_;
};

inline std::string readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    REQUIRE(f.good());
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

inline std::vector<std::uint8_t> readBytes(const std::filesystem::path& p) {
    const std::string s = readFile(p);
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

// One compile + run of a small program, shared by every test that needs a `RunResult`.
struct Fixture {
    core::EventBus bus;
    runtime::Session session{&bus};
    std::string source;
    const compiler::CompiledProgram* compiled = nullptr;
    runtime::RunResult result;

    Fixture(std::string_view deviceId, std::string_view body, runtime::RunOptions options) {
        auto st = session.selectDevice(deviceId);
        INFO("selectDevice: " << (st ? std::string() : st.error().format()));
        REQUIRE(st.has_value());
        source = "OPENQASM 3.0;\ninclude \"stdgates.inc\";\n" + std::string(body);
        auto id = session.loadProgram(source, "report_test.qasm");
        REQUIRE(id.has_value());
        auto handle = session.compile(*id);
        REQUIRE(handle.has_value());
        auto compileStatus = session.waitCompile(*handle);
        INFO("compile: " << (compileStatus ? std::string() : compileStatus.error().format()));
        REQUIRE(compileStatus.has_value());
        compiled = session.compiled(*handle);
        REQUIRE(compiled != nullptr);

        runtime::RunRequest request;
        request.program = compiled;
        request.source = session.program(*id);
        request.options = std::move(options);
        auto out = session.runSync(request);
        INFO("run: " << (out ? std::string() : out.error().format()));
        REQUIRE(out.has_value());
        result = std::move(*out);
    }
};

// A Bell pair on a 5-qubit fixed-frequency transmon device, 256 shots, seeded.
inline const Fixture& bell() {
    static const std::unique_ptr<Fixture> f = [] {
        runtime::RunOptions o;
        o.shots = 256;
        o.seed = 20250916ull;
        o.cadence = runtime::SnapshotCadence::End;
        o.maxSnapshots = 4;
        return std::make_unique<Fixture>(
            "sc_fixed_5", "qubit[2] q; bit[2] c;\nh q[0];\ncx q[0], q[1];\nc = measure q;\n", o);
    }();
    return *f;
}

} // namespace rtest
