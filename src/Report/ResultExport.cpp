#include "Report/ResultExport.hpp"
#include "Data/Fidelity.hpp"

#include "Data/Writers.hpp"
#include <fstream>

namespace qlab::report {
namespace {

using core::Json;

const char* regKindName(ir::RegKind k) {
    switch (k) {
    case ir::RegKind::Bit: return "bit";
    case ir::RegKind::Bool: return "bool";
    case ir::RegKind::Int: return "int";
    case ir::RegKind::Uint: return "uint";
    }
    return "bit";
}
ir::RegKind regKindFrom(std::string_view s) {
    if (s == "bool") return ir::RegKind::Bool;
    if (s == "int") return ir::RegKind::Int;
    if (s == "uint") return ir::RegKind::Uint;
    return ir::RegKind::Bit;
}

std::size_t bytesPerColumn(std::uint64_t shots) { return static_cast<std::size_t>((shots + 7) / 8); }

} // namespace

std::vector<std::uint8_t> packMemory(const runtime::RunResult& r) {
    const std::uint64_t shots = r.memory.size();
    const std::uint32_t bits = r.layout.bits;
    const std::size_t stride = bytesPerColumn(shots);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(bits) * stride, 0);
    for (std::uint64_t s = 0; s < shots; ++s) {
        const auto& shot = r.memory[static_cast<std::size_t>(s)];
        for (std::uint32_t b = 0; b < bits; ++b) {
            if (b >= shot.bits.size() || shot.bits[b] == 0) continue;
            out[static_cast<std::size_t>(b) * stride + static_cast<std::size_t>(s / 8)] |=
                static_cast<std::uint8_t>(1u << (s % 8));
        }
    }
    return out;
}

core::Json memoryHeaderJson(const runtime::RunResult& r) {
    Json j;
    j["shots"] = r.memory.size();
    j["bits"] = r.layout.bits;
    j["packing"] = "bit_column_le8";   // 8 shots per byte per bit column, LSB = first shot
    j["registers"] = Json::array();
    for (const auto& reg : r.layout.registers)
        j["registers"].push_back({{"name", reg.name},
                                  {"first", reg.first},
                                  {"size", reg.size},
                                  {"kind", regKindName(reg.kind)},
                                  {"scalar", reg.scalar},
                                  {"output", reg.output}});
    return j;
}

Result<runtime::ClassicalLayout> layoutFromJson(const core::Json& header) {
    if (!header.is_object() || !header.contains("bits"))
        return fail(ErrorCode::Parse, "memory header: missing 'bits'");
    runtime::ClassicalLayout layout;
    layout.bits = header["bits"].get<std::uint32_t>();
    if (header.contains("registers") && header["registers"].is_array()) {
        for (const auto& e : header["registers"]) {
            runtime::RegisterInfo reg;
            reg.name = e.value("name", "");
            reg.first = e.value("first", 0u);
            reg.size = e.value("size", 1u);
            reg.kind = regKindFrom(e.value("kind", "bit"));
            reg.scalar = e.value("scalar", false);
            reg.output = e.value("output", false);
            layout.registers.push_back(std::move(reg));
        }
    }
    return layout;
}

Result<data::Histogram> decodeMemory(std::span<const std::uint8_t> blob, const core::Json& header) {
    QXL_TRY_ASSIGN(const runtime::ClassicalLayout layout, layoutFromJson(header));
    if (!header.contains("shots")) return fail(ErrorCode::Parse, "memory header: missing 'shots'");
    const std::uint64_t shots = header["shots"].get<std::uint64_t>();
    const std::size_t stride = bytesPerColumn(shots);
    if (blob.size() != static_cast<std::size_t>(layout.bits) * stride)
        return fail(ErrorCode::Parse, "memory file is " + std::to_string(blob.size()) + " bytes, expected " +
                                          std::to_string(static_cast<std::size_t>(layout.bits) * stride));
    data::Histogram h(layout.bits);
    std::vector<std::uint8_t> shot(layout.bits, 0);
    for (std::uint64_t s = 0; s < shots; ++s) {
        for (std::uint32_t b = 0; b < layout.bits; ++b) {
            const std::uint8_t byte = blob[static_cast<std::size_t>(b) * stride + static_cast<std::size_t>(s / 8)];
            shot[b] = static_cast<std::uint8_t>((byte >> (s % 8)) & 1u);
        }
        h.add(layout.key(shot));
    }
    return h;
}

// ---------------------------------------------------------------- the document (§6)

core::Json runResultJson(const runtime::RunResult& r, const ResultExportOptions& options) {
    ensureKinds();
    Json j;
    const RunIdentity id = RunIdentity::of(r);
    j["identity"] = id.toJson();
    j["program"] = {{"hash", id.programHash}, {"source", options.source}};
    j["device"] = r.device;
    j["calibrationTime"] = r.calibrationTimestamp;
    j["backend"] = id.backend;
    j["backendReason"] = r.backendReason;
    j["class"] = data::fidelityName(r.backendClass);
    j["shots"] = r.options.shots;
    j["shotsCompleted"] = r.shotsCompleted;
    j["partial"] = r.partial;
    j["seed"] = r.seed;
    j["counts"] = data::histogramToJson(r.counts);
    if (options.writeMemory && !r.memory.empty()) {
        j["memory"] = "path: " + options.memoryFile;
        j["memoryHeader"] = memoryHeaderJson(r);
    }
    j["expectations"] = Json::object();
    for (const auto& e : r.expectations) {
        Json v;
        v["value"] = e.value;
        v["stderr"] = e.stderr_;
        v["class"] = data::fidelityName(e.cls);
        if (e.exact) v["exact"] = *e.exact;
        j["expectations"][e.observable] = std::move(v);
    }
    j["marginals"] = Json::array();
    for (const auto& m : r.marginals)
        j["marginals"].push_back({{"register", m.register_},
                                  {"bit", m.bit},
                                  {"qubit", m.qubit},
                                  {"p1", m.p1},
                                  {"stderr", m.stderr_}});
    j["estimate"] = r.estimate.toJson();
    Json gates = Json::object();
    if (r.circuit)
        for (const auto& [name, n] : r.circuit->gateCounts()) gates[name] = n;
    j["resources"] = {{"qubits", r.qubits.size()},
                      {"depth", r.metrics.depth},
                      {"gates", gates},
                      {"gateCount", r.metrics.gateCount},
                      {"twoQubit", r.metrics.twoQubitCount},
                      {"tCount", r.metrics.tCount},
                      {"swaps", r.metrics.swapCount},
                      {"classicalBits", r.layout.bits}};
    if (!options.channelsFile.empty()) j["channels"] = "path: " + options.channelsFile;
    j["timing"] = {{"compile_ms", options.compileTime.count()},
                   {"run_ms", std::chrono::duration_cast<std::chrono::milliseconds>(r.wallTime).count()}};
    j["diagnostics"] = Json::array();
    for (const auto& d : r.diagnostics)
        j["diagnostics"].push_back({{"id", d.id()}, {"message", d.error.message}});
    return j;
}

std::string serializeRunResult(const runtime::RunResult& r, const ResultExportOptions& options) {
    ensureKinds();
    return core::JsonEnvelope::serialize(kRunResultKind, runResultJson(r, options), kRunResultSchema);
}

Status writeRunResult(const std::filesystem::path& path, const runtime::RunResult& r,
                      const ResultExportOptions& options) {
    const Json doc = runResultJson(r, options);
    if (!allFinite(doc)) return fail(ErrorCode::InvalidArgument, "run result holds a non-finite number");
    QXL_TRY(core::writeTextFileAtomic(path, core::JsonEnvelope::serialize(kRunResultKind, doc, kRunResultSchema)));
    if (!doc.contains("memory")) return {};

    const std::filesystem::path bin = path.parent_path() / options.memoryFile;
    const std::vector<std::uint8_t> blob = packMemory(r);
    std::error_code ec;
    if (bin.has_parent_path()) std::filesystem::create_directories(bin.parent_path(), ec);
    std::filesystem::path tmp = bin;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return fail(ErrorCode::Io, "cannot write " + tmp.string());
        if (!blob.empty()) f.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
    }
    std::filesystem::rename(tmp, bin, ec);
    if (ec) return fail(ErrorCode::Io, "rename failed: " + ec.message());
    return {};
}

Status verifyRunResult(const std::filesystem::path& path) {
    ensureKinds();
    QXL_TRY_ASSIGN(const core::Envelope env, core::JsonEnvelope::load(path, kRunResultKind));
    const Json& d = env.data;
    if (!d.contains("memory")) return {};   // nothing to check against
    if (!d.contains("memoryHeader")) return fail(ErrorCode::Parse, "run_result: 'data.memoryHeader' is missing");
    const std::string ref = d["memory"].get<std::string>();
    const std::string rel = ref.starts_with("path: ") ? ref.substr(6) : ref;
    const std::filesystem::path bin = path.parent_path() / rel;
    std::ifstream f(bin, std::ios::binary);
    if (!f) return fail(ErrorCode::Io, "cannot open " + bin.string());
    const std::vector<std::uint8_t> blob((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    QXL_TRY_ASSIGN(const data::Histogram decoded, decodeMemory(blob, d["memoryHeader"]));
    QXL_TRY_ASSIGN(const data::Histogram stored, data::histogramFromJson(d["counts"]));
    if (decoded.raw() != stored.raw())
        return fail(ErrorCode::Parse, "memory file does not decode to the counts of " + path.string());
    return {};
}

// ---------------------------------------------------------------- sweeps (§6)

std::string sweepToCsv(const runtime::SweepResult& sweep) {
    const std::size_t axes = sweep.axes.size();
    std::vector<std::vector<double>> columns(axes + 2);
    for (const auto& p : sweep.points) {
        for (std::size_t a = 0; a < axes; ++a) columns[a].push_back(a < p.coords.size() ? p.coords[a] : 0.0);
        columns[axes].push_back(p.p1);
        columns[axes + 1].push_back(p.p1Stderr);
    }
    std::vector<data::CsvColumn> cols;
    for (std::size_t a = 0; a < axes; ++a) cols.push_back({sweep.axes[a].input, sweep.axes[a].unit, columns[a]});
    cols.push_back({"p1", "", columns[axes]});
    cols.push_back({"sigma", "", columns[axes + 1]});
    return data::toCsv(cols);
}

Status writeSweepCsv(const std::filesystem::path& path, const runtime::SweepResult& sweep) {
    return core::writeTextFileAtomic(path, sweepToCsv(sweep));
}

} // namespace qlab::report
