// Spec 15 §1, §4, §10 — names, the classical layout, and the QL5xxx diagnostic bridge.
#include "Runtime/Types.hpp"
#include <algorithm>

namespace qlab::runtime {

std::string_view backendChoiceName(BackendChoice c) {
    switch (c) {
    case BackendChoice::Auto:
        return "auto";
    case BackendChoice::StateVector:
        return "statevector";
    case BackendChoice::DensityMatrix:
        return "densitymatrix";
    case BackendChoice::Stabilizer:
        return "stabilizer";
    case BackendChoice::Lindblad:
        return "lindblad";
    }
    return "?";
}

std::string_view noiseSourceName(NoiseSource n) {
    switch (n) {
    case NoiseSource::Ideal:
        return "ideal";
    case NoiseSource::Calibrated:
        return "calibrated";
    case NoiseSource::Custom:
        return "custom";
    }
    return "?";
}

std::string_view snapshotCadenceName(SnapshotCadence c) {
    switch (c) {
    case SnapshotCadence::None:
        return "none";
    case SnapshotCadence::Gate:
        return "gate";
    case SnapshotCadence::Layer:
        return "layer";
    case SnapshotCadence::Barrier:
        return "barrier";
    case SnapshotCadence::End:
        return "end";
    }
    return "?";
}

SnapshotCadence resolveCadence(SnapshotCadence c, std::uint32_t n) {
    // Spec 15 §4: per gate up to 20 qubits, per barrier above.
    if (n > 20 && (c == SnapshotCadence::Gate || c == SnapshotCadence::Layer))
        return SnapshotCadence::Barrier;
    return c;
}

lang::Diagnostic diagnostic(std::string_view id, SourceSpan span) {
    return lang::Diagnostics::make(id, std::move(span));
}

Error error(const lang::Diagnostic& d) {
    return d.error;
}

lang::Diagnostic note(std::string message, lang::Severity severity) {
    lang::Diagnostic d;
    d.severity = severity;
    d.error = Error(severity == lang::Severity::Error ? err::Unsupported : ErrorCode::Ok,
                    std::move(message));
    return d;
}

std::string ClassicalLayout::key(std::span<const std::uint8_t> shotBits) const {
    std::string s;
    s.reserve(bits);
    for (std::uint32_t i = bits; i-- > 0;)
        s.push_back(i < shotBits.size() && shotBits[i] != 0 ? '1' : '0');
    return s;
}

std::uint64_t ClassicalLayout::value(std::span<const std::uint8_t> shotBits,
                                     const RegisterInfo& r) const {
    std::uint64_t v = 0;
    const std::uint32_t w = std::min<std::uint32_t>(r.size, 64);
    for (std::uint32_t i = 0; i < w; ++i) {
        const std::uint32_t b = r.first + i;
        if (b < shotBits.size() && shotBits[b] != 0)
            v |= std::uint64_t{1} << i; // little-endian (README)
    }
    return v;
}

const RegisterInfo* ClassicalLayout::find(std::string_view name) const {
    auto it = std::find_if(registers.begin(), registers.end(),
                           [&](const RegisterInfo& r) { return r.name == name; });
    return it == registers.end() ? nullptr : &*it;
}

} // namespace qlab::runtime
