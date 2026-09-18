// Spec 23 §6, §7, §12 — the `run_result` document, the packed per-shot memory, the sweep CSV and
// the trace CSV with its provenance comments.
#include "ReportTestUtil.hpp"
#include "Data/Fidelity.hpp"

#include "Data/Writers.hpp"

using namespace qlab;
using namespace qlab::report;

TEST_CASE("run_result: the document records counts, identity and resources (spec 23 §6)") {
    const rtest::Fixture& f = rtest::bell();
    rtest::Sandbox box("run_result");
    const std::filesystem::path file = box / "runs" / "run-000001.json";

    ResultExportOptions options;
    options.source = f.source;
    options.compileTime = std::chrono::milliseconds{12};
    REQUIRE(writeRunResult(file, f.result, options).has_value());

    const core::Json doc = core::Json::parse(rtest::readFile(file));
    CHECK(doc["qxl"]["kind"] == "run_result");
    CHECK(doc["qxl"]["schema"].get<int>() == 1);
    const core::Json& d = doc["data"];

    // Run identity: program hash, device hash, backend, shots, seed.
    CHECK(d["identity"]["program_hash"].get<std::string>().starts_with("fnv1a64:"));
    CHECK(d["identity"]["device_hash"].get<std::string>().starts_with("fnv1a64:"));
    CHECK(d["identity"]["device"] == "sc_fixed_5");
    CHECK(d["identity"]["shots"].get<std::uint32_t>() == 256u);
    CHECK(d["identity"]["seed"].get<std::uint64_t>() == 20250916ull);
    CHECK(d["identity"]["backend"] == d["backend"]);
    CHECK(d["program"]["hash"] == d["identity"]["program_hash"]);
    CHECK(d["program"]["source"].get<std::string>() == f.source);
    CHECK(d["device"] == "sc_fixed_5");
    CHECK(d["calibrationTime"].get<std::string>() == f.result.calibrationTimestamp);
    CHECK(d["seed"].get<std::uint64_t>() == f.result.seed);

    // Counts: every shot is accounted for and the Bell pair dominates.
    CHECK(d["counts"]["total"].get<std::uint64_t>() == f.result.counts.total());
    CHECK(f.result.counts.total() == 256u);
    const double p00 = f.result.counts.probability("00");
    const double p11 = f.result.counts.probability("11");
    CHECK(p00 + p11 > 0.9);

    CHECK(d["resources"]["depth"].get<std::uint32_t>() == f.result.metrics.depth);
    CHECK(d["resources"]["twoQubit"].get<std::uint32_t>() == f.result.metrics.twoQubitCount);
    CHECK(d["resources"]["gates"].is_object());
    CHECK(d["timing"]["compile_ms"].get<int>() == 12);
    CHECK(d["timing"]["run_ms"].get<long long>() >= 0);
    CHECK(d["estimate"].is_object());
    CHECK(d["class"].get<std::string>() == std::string(data::fidelityName(f.result.backendClass)));
}

TEST_CASE("run_result: memory.bin decodes to the counts in the same document (spec 23 §6, §12)") {
    const rtest::Fixture& f = rtest::bell();
    REQUIRE_FALSE(f.result.memory.empty());
    rtest::Sandbox box("memory_bin");
    const std::filesystem::path file = box / "run.json";
    REQUIRE(writeRunResult(file, f.result).has_value());

    const core::Json doc = core::Json::parse(rtest::readFile(file));
    const core::Json& d = doc["data"];
    CHECK(d["memory"] == "path: memory.bin");
    CHECK(d["memoryHeader"]["shots"].get<std::uint64_t>() == f.result.memory.size());
    CHECK(d["memoryHeader"]["bits"].get<std::uint32_t>() == f.result.layout.bits);

    // 8 shots per byte per bit column: 256 shots x 2 bits = 64 bytes.
    const std::vector<std::uint8_t> blob = rtest::readBytes(box / "memory.bin");
    const std::size_t expected = static_cast<std::size_t>(f.result.layout.bits) * ((f.result.memory.size() + 7) / 8);
    CHECK(blob.size() == expected);
    CHECK(blob.size() == 64u);

    auto decoded = decodeMemory(blob, d["memoryHeader"]);
    REQUIRE(decoded.has_value());
    CHECK(decoded->total() == f.result.counts.total());
    CHECK(decoded->raw() == f.result.counts.raw());
    CHECK(verifyRunResult(file).has_value());

    // A corrupted memory file is caught by the consistency check.
    std::vector<std::uint8_t> broken = blob;
    broken[0] = static_cast<std::uint8_t>(~broken[0]);
    std::ofstream out(box / "memory.bin", std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(broken.data()), static_cast<std::streamsize>(broken.size()));
    out.close();
    auto bad = verifyRunResult(file);
    CHECK_FALSE(bad.has_value());
}

TEST_CASE("run_result: the packing of spec 23 §6 is bit-column major, 8 shots per byte") {
    runtime::RunResult r;
    r.layout.bits = 3;
    r.layout.registers.push_back({"c", 0, 3, ir::RegKind::Bit, false, false});
    // Shot s writes bit b = (s + b) % 2: an explicit pattern the packing must reproduce.
    for (std::uint32_t s = 0; s < 10; ++s) {
        runtime::ShotRecord shot;
        shot.bits = {static_cast<std::uint8_t>((s + 0) % 2), static_cast<std::uint8_t>((s + 1) % 2),
                     static_cast<std::uint8_t>((s + 2) % 2)};
        r.memory.push_back(std::move(shot));
    }
    r.counts = data::Histogram(r.layout.bits);
    for (const auto& shot : r.memory) r.counts.add(r.layout.key(shot.bits));

    const std::vector<std::uint8_t> blob = packMemory(r);
    REQUIRE(blob.size() == 3u * 2u);   // 3 columns x ceil(10/8) bytes
    for (std::uint32_t b = 0; b < 3; ++b)
        for (std::uint32_t s = 0; s < 10; ++s) {
            const std::uint8_t byte = blob[b * 2 + s / 8];
            const std::uint8_t bit = static_cast<std::uint8_t>((byte >> (s % 8)) & 1u);
            INFO("bit " << b << " shot " << s);
            CHECK(bit == (s + b) % 2);
        }
    auto decoded = decodeMemory(blob, memoryHeaderJson(r));
    REQUIRE(decoded.has_value());
    CHECK(decoded->raw() == r.counts.raw());

    // A truncated file is refused rather than decoded into wrong counts.
    std::vector<std::uint8_t> shortBlob(blob.begin(), blob.end() - 1);
    CHECK_FALSE(decodeMemory(shortBlob, memoryHeaderJson(r)).has_value());
}

TEST_CASE("sweeps: one row per point with units in the header (spec 23 §6)") {
    runtime::SweepResult sweep;
    sweep.axes.push_back({"delay", "us", {0.0, 1.0, 2.0}});
    for (int i = 0; i < 3; ++i) {
        runtime::SweepPoint p;
        p.coords = {static_cast<double>(i)};
        p.p1 = 0.5 * static_cast<double>(i);
        p.p1Stderr = 0.01 * static_cast<double>(i + 1);
        sweep.points.push_back(std::move(p));
    }
    const std::string csv = sweepToCsv(sweep);
    INFO(csv);
    CHECK(csv.starts_with("delay (us),p1 (),sigma ()\n"));
    CHECK(csv.find("\n0,0,0.01\n") != std::string::npos);
    CHECK(csv.find("\n2,1,0.03\n") != std::string::npos);

    // Two axes: long format, one column per axis then the value and its error.
    runtime::SweepResult grid;
    grid.axes.push_back({"amp", "", {0.0, 1.0}});
    grid.axes.push_back({"t", "ns", {0.0, 1.0}});
    for (int a = 0; a < 2; ++a)
        for (int t = 0; t < 2; ++t) {
            runtime::SweepPoint p;
            p.coords = {static_cast<double>(a), static_cast<double>(t)};
            p.p1 = 0.25 * static_cast<double>(2 * a + t);
            grid.points.push_back(std::move(p));
        }
    const std::string longCsv = sweepToCsv(grid);
    INFO(longCsv);
    CHECK(longCsv.starts_with("amp (),t (ns),p1 (),sigma ()\n"));
    CHECK(longCsv.find("\n1,1,0.75,0\n") != std::string::npos);
}

TEST_CASE("traces: CSV keeps its provenance in '#' comments before the header (spec 23 §7)") {
    data::Trace2D t;
    t.name = "vna.s21";
    t.xUnit = "GHz";
    t.yUnit = "dB";
    t.x = {6.0, 6.1, 6.2};
    t.y = {-3.0, -12.5, -3.5};
    t.yIm = std::vector<double>{0.0, 0.5, 0.25};
    t.sigma = std::vector<double>{0.1, 0.1, 0.1};
    t.cls = data::FidelityClass::Numerical;

    TraceProvenance p;
    p.instrument = "vna";
    p.timestamp = "2026-09-18T12:00:00Z";
    p.settings = {{"span_hz", "2e8"}, {"rbw_hz", "1e3"}, {"averages", "16"}, {"sample_rate_hz", "1e6"}};
    const std::string csv = traceToCsv(t, p);
    INFO(csv);

    CHECK(csv.starts_with("# trace: vna.s21\n"));
    CHECK(csv.find("# class: Numerical\n") != std::string::npos);
    CHECK(csv.find("# instrument: vna\n") != std::string::npos);
    CHECK(csv.find("# time: 2026-09-18T12:00:00Z\n") != std::string::npos);
    CHECK(csv.find("# rbw_hz: 1e3\n") != std::string::npos);
    // Complex traces are re/im column pairs; the header carries the units (spec 22 §7).
    const std::size_t header = csv.find("x (GHz)");
    REQUIRE(header != std::string::npos);
    CHECK(csv.compare(header, 40, "x (GHz),y_re (dB),y_im (dB),sigma (dB)\n6", 40) == 0);
    // Everything before the header is a comment line, so the file stays a valid CSV.
    std::size_t at = 0;
    while (at < header) {
        CHECK(csv[at] == '#');
        at = csv.find('\n', at) + 1;
    }
}
