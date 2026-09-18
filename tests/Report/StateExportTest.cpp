// Spec 23 §9, §12 — Simulator-only state export: the `.npy` header bytes follow the NumPy format
// spec, the payload re-reads to the amplitudes it was written from, and every file carries a
// sidecar with the qubit order and the Simulator-only notice.
#include "ReportTestUtil.hpp"
#include "Data/Fidelity.hpp"

#include <cstring>
#include <numbers>

using namespace qlab;
using namespace qlab::report;

namespace {

// Bytes 0..9 of a `.npy` v1.0 file, then the padded header dict.
struct NpyFile {
    std::vector<std::uint8_t> bytes;
    std::string dict;
    std::size_t dataOffset = 0;
};

NpyFile readNpy(const std::filesystem::path& p) {
    NpyFile f;
    f.bytes = rtest::readBytes(p);
    REQUIRE(f.bytes.size() > 10);
    CHECK(std::memcmp(f.bytes.data(), "\x93NUMPY", 6) == 0);   // magic
    CHECK(f.bytes[6] == 1);                                    // major version
    CHECK(f.bytes[7] == 0);                                    // minor version
    const std::size_t len = static_cast<std::size_t>(f.bytes[8]) | (static_cast<std::size_t>(f.bytes[9]) << 8);
    REQUIRE(f.bytes.size() >= 10 + len);
    CHECK((10 + len) % 64 == 0);                               // header is 64-byte aligned
    f.dict.assign(reinterpret_cast<const char*>(f.bytes.data() + 10), len);
    CHECK(f.dict.back() == '\n');                              // header ends with a newline
    f.dataOffset = 10 + len;
    return f;
}

std::vector<num::Complex> readComplex(const NpyFile& f) {
    const std::size_t n = (f.bytes.size() - f.dataOffset) / sizeof(num::Complex);
    std::vector<num::Complex> out(n);
    std::memcpy(out.data(), f.bytes.data() + f.dataOffset, n * sizeof(num::Complex));
    return out;
}

qsim::Snapshot bellStateVector() {
    qsim::Snapshot s;
    s.kind = qsim::Kind::StateVector;
    s.nQubits = 2;
    s.levels = 2;
    s.gateIndex = 7;
    s.simTimePs = 1172.16;
    const double r = 1.0 / std::numbers::sqrt2;
    s.amplitudes = std::vector<num::Complex>{{r, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {r, 0.0}};
    s.cls = data::FidelityClass::Exact;
    return s;
}

StateContext context() {
    StateContext ctx;
    ctx.identity = RunIdentity::of(0x1234u, "sc_fixed_5", "2026-09-17T00:00:00Z", "state_vector", 256, 20250916ull);
    ctx.qubits = {1, 2};
    ctx.levels = 2;
    return ctx;
}

} // namespace

TEST_CASE("npy: the header is exactly what the NumPy 1.0 format specifies") {
    const std::size_t shape1[1] = {4};
    const std::string h = npyHeader("<c16", shape1);
    CHECK(h.size() == 128u);                           // 10 byte prefix + 118 byte padded dict
    CHECK(h.compare(0, 6, "\x93NUMPY") == 0);
    CHECK(static_cast<std::uint8_t>(h[6]) == 1);
    CHECK(static_cast<std::uint8_t>(h[7]) == 0);
    const std::size_t len = static_cast<std::uint8_t>(h[8]) | (static_cast<std::size_t>(static_cast<std::uint8_t>(h[9])) << 8);
    CHECK(len == 118u);
    CHECK(h.substr(10, 58) == "{'descr': '<c16', 'fortran_order': False, 'shape': (4,), }");
    CHECK(h.back() == '\n');

    // 2-D shapes are written without the trailing comma; 0-d as `()`.
    const std::size_t shape2[2] = {4, 4};
    CHECK(npyHeader("<c16", shape2).substr(10, 60) == "{'descr': '<c16', 'fortran_order': False, 'shape': (4, 4), }");
    CHECK(npyHeader("<f8", {}).find("'shape': (), ") != std::string::npos);
    CHECK(npyHeader("<c16", shape1, true).find("'fortran_order': True") != std::string::npos);
    // Whatever the shape, the header stays 64-byte aligned.
    for (std::size_t n = 1; n < 40; ++n) {
        const std::size_t shape[1] = {n * 7919};
        CHECK(npyHeader("<c16", shape).size() % 64 == 0);
    }
}

TEST_CASE("state export: the state vector re-reads to the amplitudes it was written from (§9, §12)") {
    rtest::Sandbox box("state_vector");
    const qsim::Snapshot s = bellStateVector();
    REQUIRE(exportStateVector(box / "state", s, context()).has_value());

    const NpyFile f = readNpy(box / "state.npy");
    CHECK(f.dict.find("'descr': '<c16'") != std::string::npos);
    CHECK(f.dict.find("'shape': (4,)") != std::string::npos);
    CHECK(f.bytes.size() == f.dataOffset + 4 * 16);

    const std::vector<num::Complex> back = readComplex(f);
    REQUIRE(back.size() == 4u);
    for (std::size_t i = 0; i < back.size(); ++i) {
        INFO("amplitude " << i);
        CHECK(std::abs(back[i] - (*s.amplitudes)[i]) < 1e-15);
    }
    // Little-endian qubit order: |00> and |11> are indices 0 and 3 (README convention).
    CHECK(std::abs(back[0] - back[3]) < 1e-15);
    CHECK(std::abs(back[1]) < 1e-15);
    CHECK(std::abs(std::norm(back[0]) + std::norm(back[3]) - 1.0) < 1e-15);

    const core::Json doc = core::Json::parse(rtest::readFile(box / "state.json"));
    CHECK(doc["qxl"]["kind"] == "state_export");
    const core::Json& d = doc["data"];
    CHECK(d["simulator_only"].get<bool>());
    CHECK(d["notice"].get<std::string>().find("Simulator-only") != std::string::npos);
    CHECK(d["file"] == "state.npy");
    CHECK(d["dtype"] == "<c16");
    CHECK(d["shape"][0].get<std::size_t>() == 4u);
    CHECK(d["order"] == "C");
    CHECK(d["qubit_order"]["convention"].get<std::string>().find("little-endian") != std::string::npos);
    CHECK(d["qubit_order"]["qubits"][0].get<std::uint32_t>() == 1u);   // simulator 0 -> device 1
    CHECK(d["gate_index"].get<std::uint64_t>() == 7u);
    CHECK(d["class"] == "Exact");
    CHECK(d["identity"]["device"] == "sc_fixed_5");
    CHECK(d["identity"]["seed"].get<std::uint64_t>() == 20250916ull);
}

TEST_CASE("state export: a density matrix is written as a (d, d) `<c16` array (spec 23 §9)") {
    rtest::Sandbox box("density");
    qsim::Snapshot s;
    s.kind = qsim::Kind::DensityMatrix;
    s.nQubits = 2;
    num::Matrix rho(4, 4);
    const double half = 0.5;
    rho(0, 0) = half;
    rho(0, 3) = half;
    rho(3, 0) = half;
    rho(3, 3) = half;
    s.densityMatrix = rho;
    s.cls = data::FidelityClass::Numerical;
    REQUIRE(exportDensityMatrix(box / "rho", s, context()).has_value());

    const NpyFile f = readNpy(box / "rho.npy");
    CHECK(f.dict.find("'shape': (4, 4)") != std::string::npos);
    const std::vector<num::Complex> back = readComplex(f);
    REQUIRE(back.size() == 16u);
    // C order, row-major: element (i, j) is at i * 4 + j.
    CHECK(std::abs(back[0] - num::Complex{0.5, 0.0}) < 1e-15);
    CHECK(std::abs(back[3] - num::Complex{0.5, 0.0}) < 1e-15);
    CHECK(std::abs(back[12] - num::Complex{0.5, 0.0}) < 1e-15);
    CHECK(std::abs(back[15] - num::Complex{0.5, 0.0}) < 1e-15);
    CHECK(std::abs(back[1]) < 1e-15);
    num::Complex trace{0.0, 0.0};
    for (std::size_t i = 0; i < 4; ++i) trace += back[i * 4 + i];
    CHECK(std::abs(trace - num::Complex{1.0, 0.0}) < 1e-15);

    const core::Json d = core::Json::parse(rtest::readFile(box / "rho.json"))["data"];
    CHECK(d["shape"] == core::Json({4, 4}));
    CHECK(d["class"] == "Numerical");
    CHECK(d["simulator_only"].get<bool>());
}

TEST_CASE("state export: a stabilizer tableau is written as signed Pauli strings (spec 23 §9)") {
    rtest::Sandbox box("tableau");
    qsim::Snapshot s;
    s.kind = qsim::Kind::Stabilizer;
    s.nQubits = 2;
    qsim::TableauExport t;
    t.n = 2;
    t.stabilizers = {"+XX", "+ZZ"};
    t.destabilizers = {"+IZ", "+XI"};
    s.tableau = t;
    REQUIRE(exportTableau(box / "tableau", s, context()).has_value());

    const std::string text = rtest::readFile(box / "tableau.txt");
    INFO(text);
    CHECK(text.find("# stabilizers\n+XX\n+ZZ\n") != std::string::npos);
    CHECK(text.find("# destabilizers\n+IZ\n+XI\n") != std::string::npos);
    const core::Json d = core::Json::parse(rtest::readFile(box / "tableau.json"))["data"];
    CHECK(d["file"] == "tableau.txt");
    CHECK(d["dtype"] == "pauli_strings");
    CHECK(d["simulator_only"].get<bool>());
}

TEST_CASE("state export: a backend that holds no such state is refused, never faked") {
    rtest::Sandbox box("state_refuse");
    qsim::Snapshot stabilizer;
    stabilizer.kind = qsim::Kind::Stabilizer;
    auto noVector = exportStateVector(box / "x", stabilizer, context());
    REQUIRE_FALSE(noVector.has_value());
    CHECK(noVector.error().code == ErrorCode::Unsupported);
    CHECK(noVector.error().message.find("stabilizer") != std::string::npos);
    CHECK_FALSE(exportDensityMatrix(box / "x", stabilizer, context()).has_value());
    CHECK_FALSE(exportTableau(box / "x", bellStateVector(), context()).has_value());

    // A shape that does not match the data is a programming error, not a truncated file.
    const std::size_t wrong[1] = {8};
    const std::vector<num::Complex> four(4, num::Complex{});
    auto mismatch = writeNpyComplex(box / "x.npy", four, wrong);
    REQUIRE_FALSE(mismatch.has_value());
    CHECK(mismatch.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("state export: the final state of a real run exports and round-trips") {
    const rtest::Fixture& f = rtest::bell();
    if (!f.result.finalState) {
        WARN("the run kept no final state");
        return;
    }
    rtest::Sandbox box("state_run");
    StateContext ctx;
    ctx.identity = RunIdentity::of(f.result);
    ctx.qubits = f.result.qubits;
    ctx.levels = f.result.levels;

    const qsim::Snapshot& s = *f.result.finalState;
    if (s.amplitudes) {
        REQUIRE(exportStateVector(box / "final", s, ctx).has_value());
        const NpyFile file = readNpy(box / "final.npy");
        const std::vector<num::Complex> back = readComplex(file);
        REQUIRE(back.size() == s.amplitudes->size());
        for (std::size_t i = 0; i < back.size(); ++i) CHECK(std::abs(back[i] - (*s.amplitudes)[i]) < 1e-15);
    }
    if (s.densityMatrix) {
        REQUIRE(exportDensityMatrix(box / "final_rho", s, ctx).has_value());
        const NpyFile file = readNpy(box / "final_rho.npy");
        const std::vector<num::Complex> back = readComplex(file);
        REQUIRE(back.size() == s.densityMatrix->data.size());
        num::Complex trace{0.0, 0.0};
        for (std::size_t i = 0; i < s.densityMatrix->rows; ++i) trace += back[i * s.densityMatrix->cols + i];
        CHECK(std::abs(trace.real() - 1.0) < 1e-12);
        for (std::size_t i = 0; i < back.size(); ++i) CHECK(std::abs(back[i] - s.densityMatrix->data[i]) < 1e-15);
    }
    CHECK((s.amplitudes || s.densityMatrix || s.tableau));
}
