#include "Report/Types.hpp"

#include <cmath>
#include <format>
#include <mutex>

namespace qlab::report {

void ensureKinds() {
    static std::once_flag once;
    std::call_once(once, [] {
        core::JsonEnvelope::registerKind(std::string(kProjectKind), kProjectSchema);
        core::JsonEnvelope::registerKind(std::string(kRunResultKind), kRunResultSchema);
        core::JsonEnvelope::registerKind(std::string(kStateKind), kStateSchema);
    });
}

std::uint64_t fnv1a64(std::string_view text) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string hashHex(std::uint64_t h) {
    return std::format("fnv1a64:{:016x}", h);
}

core::Json RunIdentity::toJson() const {
    core::Json j;
    j["program_hash"] = programHash;
    j["device_hash"] = deviceHash;
    j["device"] = device;
    j["calibration_time"] = calibrationTime;
    j["backend"] = backend;
    j["shots"] = shots;
    j["seed"] = seed;
    return j;
}

RunIdentity RunIdentity::of(std::uint64_t programHash, std::string_view device,
                            std::string_view calibrationTime, std::string_view backend,
                            std::uint64_t shots, std::uint64_t seed) {
    RunIdentity id;
    id.programHash = hashHex(programHash);
    id.device = device;
    id.calibrationTime = calibrationTime;
    id.backend = backend;
    id.shots = shots;
    id.seed = seed;
    id.deviceHash = hashHex(fnv1a64(std::string(device) + "@" + std::string(calibrationTime)));
    return id;
}

RunIdentity RunIdentity::of(const runtime::RunResult& r) {
    return of(r.programHash, r.device, r.calibrationTimestamp, backendName(r.backend),
              r.options.shots, r.seed);
}

std::string_view backendName(qsim::Kind k) {
    switch (k) {
    case qsim::Kind::StateVector:
        return "state_vector";
    case qsim::Kind::DensityMatrix:
        return "density_matrix";
    case qsim::Kind::Stabilizer:
        return "stabilizer";
    case qsim::Kind::Lindblad:
        return "lindblad";
    case qsim::Kind::Trajectories:
        return "trajectories";
    }
    return "unknown";
}

std::string_view backendName(runtime::BackendChoice c) {
    switch (c) {
    case runtime::BackendChoice::Auto:
        return "auto";
    case runtime::BackendChoice::StateVector:
        return "state_vector";
    case runtime::BackendChoice::DensityMatrix:
        return "density_matrix";
    case runtime::BackendChoice::Stabilizer:
        return "stabilizer";
    case runtime::BackendChoice::Lindblad:
        return "lindblad";
    }
    return "auto";
}

runtime::BackendChoice backendChoiceFrom(std::string_view name) {
    if (name == "state_vector" || name == "statevector" || name == "StateVector")
        return runtime::BackendChoice::StateVector;
    if (name == "density_matrix" || name == "densitymatrix" || name == "DensityMatrix")
        return runtime::BackendChoice::DensityMatrix;
    if (name == "stabilizer" || name == "Stabilizer")
        return runtime::BackendChoice::Stabilizer;
    if (name == "lindblad" || name == "Lindblad")
        return runtime::BackendChoice::Lindblad;
    return runtime::BackendChoice::Auto;
}

std::string_view simulatorOnlyNotice() {
    // Spec 23 §9 / spec 00 §5: the state of a physical device is not observable; these files exist
    // only because the run was simulated.
    return "Simulator-only: the full quantum state cannot be read out from a physical device. "
           "This file is the simulator's internal state, not a measurement.";
}

bool allFinite(const core::Json& j) {
    if (j.is_number_float())
        return std::isfinite(j.get<double>());
    if (j.is_array() || j.is_object())
        for (const auto& v : j)
            if (!allFinite(v))
                return false;
    return true;
}

std::string relativePathString(const std::filesystem::path& p, const std::filesystem::path& base) {
    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(p, base, ec);
    const std::filesystem::path& use = (ec || rel.empty()) ? p : rel;
    std::string s = use.generic_string();
    return s;
}

} // namespace qlab::report
