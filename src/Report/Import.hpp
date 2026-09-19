#pragma once
// Spec 23 §11 — import. v1 accepts exactly three things: OpenQASM 3 files, OpenQASM 2 files
// (converted on import, with a diagnostic per rewrite) and device directories from another
// QuantumXLab installation (copied into the user devices folder after the lint of spec 03 §4).
// No other format is accepted; `classify` reports `Unknown` for everything else.
#include "Lang/Diagnostics.hpp"
#include "Report/Types.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace qlab::report {

enum class ImportKind : std::uint8_t { Unknown, Qasm3, Qasm2, DeviceDirectory };
std::string_view importKindName(ImportKind k);

// By extension (`.qasm`, `.qasm3`, `.inc`) and first statement (`OPENQASM 2.0` → Qasm2); a
// directory holding `device.json` is a device directory.
ImportKind classify(const std::filesystem::path& path);

struct ImportedProgram {
    std::string source; // OpenQASM 3, ready for `lang::parseProgram`
    std::filesystem::path origin;
    bool converted = false;                    // an OpenQASM 2 file was rewritten
    std::vector<lang::Diagnostic> diagnostics; // one Info per rewrite (spec 23 §11)
};

// Spec 23 §11: the OpenQASM 2 → 3 rewrites this shim performs. `qreg`/`creg`, `measure q -> c;`
// and the `U`/`CX`/`u1`/`u2`/`u3` aliases need none — the front end accepts them (spec 13 §4).
ImportedProgram convertQasm2(std::string text, const std::filesystem::path& origin = {});

Result<ImportedProgram> importProgram(const std::filesystem::path& path);

// The user devices folder of spec 23 §11 (`<userData>/devices`).
std::filesystem::path userDeviceDir();
// Copies `dir` under `destRoot` (default `userDeviceDir()`) after `hw::loadDevice` accepts it.
// Returns the directory it was copied to; an existing directory of the same id is an error.
Result<std::filesystem::path> importDeviceDirectory(const std::filesystem::path& dir,
                                                    const std::filesystem::path& destRoot = {});

} // namespace qlab::report
