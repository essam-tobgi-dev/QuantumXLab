// Spec 10 §6 — pulses.json for all six shipped devices: load against the typed calibration, one
// verified schedule for every native gate on every qubit/edge, completeness, recalibration.
#include "PulseTestSupport.hpp"

#include <catch2/catch_approx.hpp>

#include <format>
#include <numbers>

using namespace qlab;
using namespace qlab::pulse;
using Catch::Approx;
using pulsetest::library;

namespace {
Result<void> verifyGate(const PulseLibrary& lib, const std::string& gate,
                        std::span<const std::uint32_t> qubits, std::size_t& count) {
    const Defcal* d = lib.find(gate, qubits);
    if (!d)
        return fail(kErrNoDefcal, "missing defcal");
    ParamMap params;
    for (auto const& p : d->params)
        params[p] = 0.7;
    QXL_TRY_ASSIGN(const Schedule s, lib.scheduleFor(gate, qubits, params));
    std::vector<Warning> warnings;
    QXL_TRY(s.verify(lib.device(), &warnings));
    ++count;
    return {};
}
} // namespace

TEST_CASE("all six shipped pulse tables load and resolve cal.* against hw::Calibration") {
    for (auto const& id : pulsetest::shippedDevices()) {
        INFO(id);
        const PulseLibrary& lib = library(id);
        REQUIRE(lib.deviceId() == id);
        REQUIRE(lib.dt().value == lib.device().timing.dtPs);
        REQUIRE(lib.granularity() == 16);
        REQUIRE(lib.minPulseSamples() == 64);
        const std::uint32_t q0[1] = {0};
        const ChannelId drive0 = lib.isIonDevice() ? ChannelId::raman(0) : ChannelId::drive(0);
        const FrameDecl* f = lib.frameFor(drive0);
        REQUIRE(f != nullptr);
        // frame frequency_ghz "cal.qubits.0.f01_ghz" is the calibrated f01 (Hz), not a copy of the
        // file
        REQUIRE(f->frequencyHz == Approx(lib.calibration().qubit(0)->f01.value.v).epsilon(1e-15));
        REQUIRE(lib.has(lib.isIonDevice() ? "rx" : "sx", q0));
    }
    const PulseLibrary& ion = library("ion_chain_11");
    const FrameDecl* g = ion.frameFor(ChannelId::globalRaman());
    REQUIRE(g != nullptr);
    REQUIRE(g->frequencyHz == Approx(12.642812118e9).epsilon(1e-15)); // device.ion.f_qubit_ghz
}

TEST_CASE("every native gate on every qubit and edge yields a schedule that passes verify") {
    for (auto const& id : pulsetest::shippedDevices()) {
        INFO(id);
        const PulseLibrary& lib = library(id);
        const hw::Device& dev = lib.device();
        std::size_t count = 0;
        std::vector<std::string> failures;
        auto check = [&](const std::string& gate, std::span<const std::uint32_t> qs) {
            if (auto r = verifyGate(lib, gate, qs, count); !r) {
                std::string where = gate + "(";
                for (std::size_t i = 0; i < qs.size(); ++i)
                    where += (i ? "," : "") + std::to_string(qs[i]);
                failures.push_back(where + "): " + r.error().message);
            }
        };
        const auto data = dev.dataQubits();
        for (auto q : data) {
            const std::uint32_t one[1] = {q};
            for (auto const& gate : dev.gates.single)
                check(gate, one);
            check("measure", one);
            check("reset", one);
        }
        if (dev.allToAll) {
            for (std::size_t i = 0; i < data.size(); ++i)
                for (std::size_t j = i + 1; j < data.size(); ++j) {
                    const std::uint32_t pair[2] = {data[i], data[j]};
                    for (auto const& gate : dev.gates.two)
                        check(gate, pair);
                }
        } else {
            for (auto const& e : dev.edges) {
                const std::uint32_t pair[2] = {e.a, e.b};
                for (auto const& gate : dev.gates.two)
                    check(gate, pair);
            }
        }
        INFO((failures.empty() ? std::string{} : failures.front()));
        REQUIRE(failures.empty());
        const std::size_t pairs =
            dev.allToAll ? data.size() * (data.size() - 1) / 2 : dev.edges.size();
        REQUIRE(count ==
                data.size() * (dev.gates.single.size() + 2) + pairs * dev.gates.two.size());
    }
}

TEST_CASE("a native gate without a defcal fails the load with E_NO_DEFCAL") {
    const auto dir = hw::deviceRoot() / "sc_fixed_5";
    auto env = core::JsonEnvelope::load(dir / "pulses.json", "pulses");
    auto loaded = hw::loadDevice(dir);
    REQUIRE(env);
    REQUIRE(loaded);
    core::Json pulses = env->data;
    auto& defcals = pulses["defcals"];
    std::size_t before = defcals.size();
    for (auto it = defcals.begin(); it != defcals.end();) {
        const bool drop = ((*it)["gate"] == "sx" && (*it)["qubits"] == core::Json::array({3})) ||
                          ((*it)["gate"] == "ecr" && (*it)["qubits"] == core::Json::array({1, 3}));
        it = drop ? defcals.erase(it) : it + 1;
    }
    REQUIRE(defcals.size() == before - 2);
    auto lib = PulseLibrary::fromJson(pulses, loaded->device, loaded->calibration);
    REQUIRE_FALSE(lib.has_value());
    REQUIRE(lib.error().code == kErrNoDefcal);
    REQUIRE(lib.error().diagnosticId == "E_NO_DEFCAL");
    REQUIRE(lib.error().notes.size() == 2);
    REQUIRE(std::find(lib.error().notes.begin(), lib.error().notes.end(), "sx(3)") !=
            lib.error().notes.end());
    REQUIRE(std::find(lib.error().notes.begin(), lib.error().notes.end(), "ecr(1,3)") !=
            lib.error().notes.end());

    // Loader errors name the field.
    core::Json broken = env->data;
    broken.erase("defcals");
    auto noDefcals = PulseLibrary::fromJson(broken, loaded->device, loaded->calibration);
    REQUIRE_FALSE(noDefcals.has_value());
    REQUIRE(noDefcals.error().message.find("defcals") != std::string::npos);
    core::Json badFrame = env->data;
    badFrame["frames"]["d0"]["frequency_ghz"] = "cal.qubits.0.f02_ghz";
    auto noField = PulseLibrary::fromJson(badFrame, loaded->device, loaded->calibration);
    REQUIRE_FALSE(noField.has_value());
    REQUIRE(noField.error().message.find("cal.qubits.0.f02_ghz") != std::string::npos);
}

TEST_CASE("cal references follow a recalibration without editing pulses.json") {
    const PulseLibrary& lib = library("sc_fixed_5");
    const hw::Calibration recal = lib.calibration().perturbed(11);
    REQUIRE(recal.qubit(2)->f01.value.v != lib.calibration().qubit(2)->f01.value.v);
    auto moved = lib.withCalibration(recal);
    REQUIRE(moved);
    for (std::uint32_t q = 0; q < 5; ++q) {
        REQUIRE(moved->frameFor(ChannelId::drive(q))->frequencyHz ==
                Approx(recal.qubit(q)->f01.value.v).epsilon(1e-15));
        REQUIRE(moved->frameFor(ChannelId::measure(q))->frequencyHz ==
                Approx(recal.qubit(q)->readoutFrequency->value.v).epsilon(1e-15));
    }
    // CR frames sit at the (recalibrated) target frequency (spec 10 §3).
    REQUIRE(moved->frameFor(ChannelId::control(3, 4))->frequencyHz ==
            Approx(recal.qubit(4)->f01.value.v).epsilon(1e-15));
    // The original library is untouched.
    REQUIRE(lib.frameFor(ChannelId::drive(2))->frequencyHz ==
            Approx(lib.calibration().qubit(2)->f01.value.v).epsilon(1e-15));
}

TEST_CASE("symmetric two-qubit gates match either qubit order, directed CR gates do not") {
    const std::uint32_t ab[2] = {0, 1};
    const std::uint32_t ba[2] = {1, 0};
    const PulseLibrary& grid = library("sc_tunable_grid_54");
    REQUIRE(grid.find("cz", ba) == grid.find("cz", ab));
    REQUIRE(grid.find("siswap", ba) != nullptr);
    const PulseLibrary& ions = library("ion_chain_11");
    REQUIRE(ions.find("ms", ba) == ions.find("ms", ab));
    const PulseLibrary& fixed = library("sc_fixed_5");
    REQUIRE(fixed.find("cx", ab) != nullptr);
    REQUIRE(fixed.find("cx", ba) == nullptr);
    auto reversed = fixed.scheduleFor("cx", {1, 0});
    REQUIRE_FALSE(reversed.has_value());
    REQUIRE(reversed.error().diagnosticId == "E_NO_DEFCAL");
    auto missingParam = fixed.scheduleFor("rz", {0});
    REQUIRE_FALSE(missingParam.has_value());
    REQUIRE(missingParam.error().message.find("theta") != std::string::npos);
}
