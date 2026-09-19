#pragma once
// Spec 09 §1 — loading a device directory. Every cross-reference failure is collected, not just the
// first.
#include "Core/Json.hpp"
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include <filesystem>

namespace qlab::hw {

struct LoadedDevice {
    Device device;
    Calibration calibration;
    std::vector<std::string>
        warnings; // model-vs-calibration disagreements (spec 09 §5.3), class Model
};

Result<Device> parseDevice(const core::Json& data, const std::filesystem::path& dir = {});
Result<Calibration> parseCalibration(const core::Json& data, const Device& dev);
Result<Device> loadDeviceJson(const std::filesystem::path& file);
Result<Calibration> loadCalibrationJson(const std::filesystem::path& file, const Device& dev);
// Loads device.json + calibration.json, validates both and their cross-references.
Result<LoadedDevice> loadDevice(const std::filesystem::path& dir);
// Enumerate device directories under a root (those containing device.json).
std::vector<std::filesystem::path> listDeviceDirs(const std::filesystem::path& root);

core::Json deviceToJson(const Device& d);
core::Json calibrationToJson(const Calibration& c);

} // namespace qlab::hw
