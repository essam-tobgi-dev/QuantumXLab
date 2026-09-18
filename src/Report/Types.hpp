#pragma once
// Spec 23 §1 — vocabulary shared by every export of this module: the envelope kinds it owns, the
// run identity that every export records, and the Simulator-only notice of §9.
//
// Envelope kinds are bare names, like the shipped assets ("device", "calibration", "wiring",
// "analysis.recipe"): `project` (§2), `run_result` (§6), `state_export` (§9).
#include "Core/Json.hpp"
#include "Data/Fidelity.hpp"
#include "Runtime/Run.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace qlab::report {

inline constexpr std::string_view kProjectKind = "project";
inline constexpr std::string_view kRunResultKind = "run_result";
inline constexpr std::string_view kStateKind = "state_export";
inline constexpr int kProjectSchema = 1;
inline constexpr int kRunResultSchema = 1;
inline constexpr int kStateSchema = 1;

// Registers the three kinds with `core::JsonEnvelope` on first use. Every entry point of the module
// calls it, so no static-initialisation order or linker `--gc-sections` question arises.
void ensureKinds();

// FNV-1a 64 of a text, the hash spec 14 §11 uses for programs (`compiler::programHash`).
std::uint64_t fnv1a64(std::string_view text);
// "fnv1a64:0123456789abcdef" — the hash prefix names the function so a reader never mistakes it
// for the SHA-256 the spec's example shows (SPEC_DEVIATIONS 55).
std::string hashHex(std::uint64_t h);

// Spec 23 §6 / brief: the identity every export records. `deviceHash` covers the device id and the
// calibration timestamp, which together pin the model a result was produced with.
struct RunIdentity {
    std::string programHash;
    std::string deviceHash;
    std::string device;
    std::string calibrationTime;
    std::string backend;
    std::uint64_t shots = 0;
    std::uint64_t seed = 0;

    core::Json toJson() const;
    static RunIdentity of(const runtime::RunResult& r);
    static RunIdentity of(std::uint64_t programHash, std::string_view device, std::string_view calibrationTime,
                          std::string_view backend, std::uint64_t shots, std::uint64_t seed);
};

// Spec 23 §9: the notice a state export carries in its sidecar and in the export dialog.
std::string_view simulatorOnlyNotice();

// The backend spelling the documents of spec 23 §2 and §6 use ("state_vector", "density_matrix",
// "stabilizer", "lindblad", "trajectories"). `qsim::kindName` is the display spelling
// ("DensityMatrix") and `runtime::backendChoiceName` the `pragma qlab.backend` one
// ("densitymatrix"); a file needs one stable name, and this is it (SPEC_DEVIATIONS 58).
std::string_view backendName(qsim::Kind k);
std::string_view backendName(runtime::BackendChoice c);
// Accepts all three spellings; `Auto` for a name no backend answers to.
runtime::BackendChoice backendChoiceFrom(std::string_view name);

// Spec 23 §1: doubles are written with a shortest round-trip representation; NaN/Inf are not
// permitted in a document. True when every number of `j` is finite.
bool allFinite(const core::Json& j);

// Spec 23 §1: paths inside files are relative to the file's directory, forward slashes.
std::string relativePathString(const std::filesystem::path& p, const std::filesystem::path& base);

} // namespace qlab::report
