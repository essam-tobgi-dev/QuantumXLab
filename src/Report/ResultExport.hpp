#pragma once
// Spec 23 §6 — results export: the `run_result` document, the packed per-shot memory beside it and
// the sweep CSV. Every document records the run identity (program hash, device hash, backend,
// shots, seed) of `report::RunIdentity`.
#include "Data/Histogram.hpp"
#include "Report/Types.hpp"
#include "Runtime/Run.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace qlab::report {

struct ResultExportOptions {
    std::string source;                       // program text stored under "program.source"
    bool writeMemory = true;                  // write `memoryFile` beside the JSON
    std::string memoryFile = "memory.bin";    // relative to the JSON's directory (§1)
    std::string channelsFile;                 // "" = the document names no channel CSV
    std::chrono::milliseconds compileTime{0}; // "timing.compile_ms" (the run supplies run_ms)
};

// Spec 23 §6: per-shot bits packed little-endian, 8 shots per byte per bit column. Byte
// `b·ceil(N/8) + s/8`, bit `s % 8` holds classical bit `b` of shot `s`; the file is
// `bits · ceil(shots/8)` bytes (10⁶ shots × 27 bits = 3.4 MB).
std::vector<std::uint8_t> packMemory(const runtime::RunResult& r);
// The header of §6: shot count and register layout, stored in the `run_result` document itself.
core::Json memoryHeaderJson(const runtime::RunResult& r);
Result<runtime::ClassicalLayout> layoutFromJson(const core::Json& header);
// Rebuilds the histogram the packed memory represents (§12 consistency check).
Result<data::Histogram> decodeMemory(std::span<const std::uint8_t> blob, const core::Json& header);

// The `run_result` document of spec 23 §6.
core::Json runResultJson(const runtime::RunResult& r, const ResultExportOptions& options = {});
std::string serializeRunResult(const runtime::RunResult& r,
                               const ResultExportOptions& options = {});
// Writes `<path>` and, when the run kept its memory, `<path's directory>/<memoryFile>`.
Status writeRunResult(const std::filesystem::path& path, const runtime::RunResult& r,
                      const ResultExportOptions& options = {});
// Spec 23 §12: re-reads a written document and checks that the memory file decodes to its counts.
Status verifyRunResult(const std::filesystem::path& path);

// Spec 23 §6 sweeps: one row per point. A single axis gives `x (unit), p1 (), sigma ()`; two or
// more give the long format with one column per axis, then `p1 ()` and `sigma ()`.
std::string sweepToCsv(const runtime::SweepResult& sweep);
Status writeSweepCsv(const std::filesystem::path& path, const runtime::SweepResult& sweep);

} // namespace qlab::report
