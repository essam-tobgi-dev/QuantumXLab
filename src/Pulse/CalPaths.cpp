// Spec 10 §6 / spec 09 §3 — `cal.*` and `device.*` references resolved against the typed
// hw::Calibration and hw::Device, in the unit the calibration.json field name carries.
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include "Pulse/Errors.hpp"
#include "Pulse/Expr.hpp"
#include <charconv>
#include <format>
#include <span>
#include <vector>

namespace qlab::pulse {
namespace {

std::vector<std::string_view> splitPath(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == '.') {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    return out;
}

std::unexpected<Error> missing(std::string_view path, std::string_view what) {
    return fail(kErrLibrary, std::format("path '{}': {}", path, what));
}

Result<std::uint32_t> parseIndex(std::string_view s, std::string_view path) {
    std::uint32_t v = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return missing(path, std::format("'{}' is not an index", s));
    return v;
}

Result<double> qubitField(const hw::QubitCal& q, std::string_view f, std::string_view path) {
    if (f == "f01_ghz") return q.f01.value.v / 1e9;
    if (f == "anharmonicity_mhz") return q.anharmonicity.value.v / 1e6;
    if (f == "t1_us") return q.t1.value.v * 1e6;
    if (f == "t2_echo_us") return q.t2echo.value.v * 1e6;
    if (f == "t2_star_us") return q.t2star.value.v * 1e6;
    if (f == "thermal_population") return q.thermalPopulation.value;
    if (f == "gate_error_1q") return q.gateError1q.value;
    if (f == "duration_1q_ns") return q.duration1q.value.v * 1e9;
    if (f == "coherent_error_fraction_1q") return q.coherentErrorFraction1q.value;
    if (f == "reset_error") return q.resetError.value;
    if (f == "readout_duration_ns") return q.readoutDuration.value.v * 1e9;
    if (f == "readout_crosstalk_dephasing") return q.readoutCrosstalkDephasing.value;
    if (f == "leakage_1q") return q.leakage1q.value;
    if (f == "readout_f_ghz" && q.readoutFrequency) return q.readoutFrequency->value.v / 1e9;
    if (f == "readout_chi_mhz" && q.readoutChi) return q.readoutChi->value.v / 1e6;
    if (f == "readout_kappa_mhz" && q.readoutKappa) return q.readoutKappa->value.v / 1e6;
    if (f == "lamb_dicke" && q.lambDicke) return q.lambDicke->value;
    return missing(path, std::format("the calibration has no qubit field '{}'", f));
}

Result<double> edgeField(const hw::EdgeCal& e, std::span<const std::string_view> rest, std::string_view path) {
    if (rest.size() == 1) {
        const std::string_view f = rest[0];
        if (f == "gate_error_2q") return e.gateError2q.value;
        if (f == "coherent_error_fraction_2q") return e.coherentErrorFraction2q.value;
        if (f == "leakage_2q") return e.leakage2q.value;
        if (f == "zz_khz") return e.zz.value.v / 1e3;
        if (f == "duration_ns") return e.duration.value.v * 1e9;
        if (f == "coupling_g_mhz") return e.couplingG.value.v / 1e6;
        if (f == "coupler" && e.coupler) return static_cast<double>(*e.coupler);
        return missing(path, std::format("the calibration has no edge field '{}'", f));
    }
    if (rest.size() == 2) { // per-gate override on the edge, e.g. cal.edges.0-1.siswap.duration_ns
        auto it = e.extraGates.find(std::string(rest[0]));
        if (it == e.extraGates.end())
            return missing(path, std::format("the edge has no '{}' calibration", rest[0]));
        if (rest[1] == "gate_error_2q") return it->second.first.value;
        if (rest[1] == "duration_ns") return it->second.second.value.v * 1e9;
        return missing(path, std::format("the '{}' calibration has no field '{}'", rest[0], rest[1]));
    }
    return missing(path, "malformed edge path");
}

Result<double> calPath(const hw::Calibration& cal, std::span<const std::string_view> p, std::string_view path) {
    if (p.size() >= 3 && p[0] == "qubits") {
        QXL_TRY_ASSIGN(const std::uint32_t q, parseIndex(p[1], path));
        const hw::QubitCal* qc = cal.qubit(q);
        if (!qc) return missing(path, std::format("no calibration for qubit {}", q));
        if (p.size() != 3) return missing(path, "malformed qubit path");
        return qubitField(*qc, p[2], path);
    }
    if (p.size() >= 3 && p[0] == "edges") {
        const auto dash = p[1].find('-');
        if (dash == std::string_view::npos) return missing(path, std::format("'{}' is not an edge key", p[1]));
        QXL_TRY_ASSIGN(const std::uint32_t a, parseIndex(p[1].substr(0, dash), path));
        QXL_TRY_ASSIGN(const std::uint32_t b, parseIndex(p[1].substr(dash + 1), path));
        const hw::EdgeCal* e = cal.edge(a, b);
        if (!e) return missing(path, std::format("no calibration for edge {}-{}", a, b));
        return edgeField(*e, p.subspan(2), path);
    }
    if (p.size() == 3 && p[0] == "motional") {
        if (!cal.motional) return missing(path, "the calibration has no motional block");
        QXL_TRY_ASSIGN(const std::uint32_t k, parseIndex(p[2], path));
        const auto& m = *cal.motional;
        if (p[1] == "axial_modes_mhz" && k < m.axialModes.size()) return m.axialModes[k].v / 1e6;
        if (p[1] == "radial_modes_mhz" && k < m.radialModes.size()) return m.radialModes[k].v / 1e6;
        if (p[1] == "equilibrium_positions_l" && k < m.equilibriumPositions.size()) return m.equilibriumPositions[k];
        return missing(path, "no such motional entry");
    }
    return missing(path, "unknown calibration path");
}

Result<double> devicePath(const hw::Device& dev, std::span<const std::string_view> p, std::string_view path) {
    if (p.size() != 2) return missing(path, "malformed device path");
    if (p[0] == "ion" && dev.ion) {
        if (p[1] == "f_qubit_ghz") return dev.ion->fQubit.v / 1e9;
        if (p[1] == "lamb_dicke_nominal") return dev.ion->lambDickeNominal;
        if (p[1] == "raman_wavelength_nm") return dev.ion->ramanWavelength.v * 1e9;
    }
    if (p[0] == "timing") {
        if (p[1] == "dt_ps") return static_cast<double>(dev.timing.dtPs);
        if (p[1] == "granularity_samples") return static_cast<double>(dev.timing.granularitySamples);
        if (p[1] == "min_pulse_samples") return static_cast<double>(dev.timing.minPulseSamples);
        if (p[1] == "readout_ns") return dev.timing.readout.v * 1e9;
        if (p[1] == "feedforward_ns") return dev.timing.feedforward.v * 1e9;
    }
    if (p[0] == "motional_modes" && dev.motionalModes) {
        if (p[1] == "omega_z_mhz") return dev.motionalModes->omegaZ.v / 1e6;
        if (p[1] == "omega_r_mhz") return dev.motionalModes->omegaR.v / 1e6;
        if (p[1] == "heating_quanta_per_s") return dev.motionalModes->heatingQuantaPerS;
    }
    return missing(path, "unknown device path");
}

} // namespace

PathResolver makeCalibrationResolver(const hw::Device& device, const hw::Calibration& calibration) {
    return [&device, &calibration](std::string_view path) -> Result<double> {
        const auto parts = splitPath(path);
        const std::span<const std::string_view> rest(parts.data() + 1, parts.size() - 1);
        if (parts.front() == "cal") return calPath(calibration, rest, path);
        if (parts.front() == "device") return devicePath(device, rest, path);
        return fail(kErrLibrary, std::format("unknown symbol '{}'", path));
    };
}

} // namespace qlab::pulse
