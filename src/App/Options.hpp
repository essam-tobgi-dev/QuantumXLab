#pragma once
// Spec 03 §3 — the command line of `quantumxlab`. Parsing is pure: it touches no global state, so
// the test drives it directly.
//
//   quantumxlab [project.qxlab]                       open the GUI (optionally on a project)
//   quantumxlab --run prog.qasm --device <id> --shots N --seed S --backend <kind> --json
//   quantumxlab --selftest <out_dir>                  headless render + example check
//   quantumxlab --version | --help
//
// `--assets <dir>` overrides the asset root in every mode (spec 03 §6); the application exports it
// as `QXL_ASSETS` before anything reads `core::assetDir()`.
#include "Core/Error.hpp"
#include "Runtime/Types.hpp"
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::app {

enum class Mode : std::uint8_t { Gui, Run, SelfTest, Version, Help };

struct Options {
    Mode mode = Mode::Gui;
    std::filesystem::path project;      // positional `.qxlab`, Gui mode
    std::filesystem::path program;      // --run
    std::filesystem::path outDir;       // --selftest
    std::filesystem::path assets;       // --assets (empty = QXL_ASSET_DIR / QXL_ASSETS)
    std::string device = "sc_fixed_5";
    std::string layout;                 // --layout; empty = the device's default lab layout
    std::uint32_t shots = 1024;
    std::uint64_t seed = 1;
    runtime::BackendChoice backend = runtime::BackendChoice::Auto;
    bool json = false;                  // --json: RunResult + Estimate as one JSON document
    bool ideal = false;                 // --ideal: no calibration-derived noise
    bool physicalLab = false;           // --physical-lab: start with the spec 00 §6 toggle on
    int optimize = 1;                   // -O0 | -O1 | -O2
    // Which of the three the command line pinned: everything else is taken from the program's
    // `pragma qlab.*` (spec 13 §7), exactly as `compiler::resolveContext` resolves compile options.
    bool shotsGiven = false, seedGiven = false, backendGiven = false, optimizeGiven = false;

    bool headless() const { return mode == Mode::Run || mode == Mode::SelfTest; }
};

// Error code block of this module (spec 04 §2). App has no block of its own in the table, so the
// generic codes are used with these names.
namespace err {
inline constexpr ErrorCode BadArgument = ErrorCode::InvalidArgument;
inline constexpr ErrorCode MissingValue = ErrorCode::InvalidArgument;
inline constexpr ErrorCode NoAsset = ErrorCode::NotFound;
inline constexpr ErrorCode NoContext = ErrorCode::Unsupported;   // no window / GL context
} // namespace err

// Parses `argv[1..]`. An unknown flag, a missing value or a malformed number is an error naming the
// offending argument; `--help` and `--version` set the mode and never fail.
Result<Options> parseOptions(std::span<const std::string_view> args);
Result<Options> parseOptions(int argc, const char* const* argv);

// The `--help` text, and the one line `--version` prints.
std::string usageText();
std::string versionText();

// The lab layout for a device id: `ion_lab_11` for ion chains, `sc_lab_standard` otherwise.
std::string defaultLayoutFor(std::string_view deviceId);

} // namespace qlab::app
