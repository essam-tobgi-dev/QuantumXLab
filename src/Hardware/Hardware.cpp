#include "Hardware/Hardware.hpp"
#include "Core/Paths.hpp"
#include <algorithm>
#include <format>

namespace qlab::hw {

std::filesystem::path deviceRoot() {
    return core::assetDir() / "Devices";
}

std::vector<std::string> shippedDeviceIds() {
    std::vector<std::string> ids;
    for (const auto& d : listDeviceDirs(deviceRoot()))
        ids.push_back(d.filename().string());
    std::sort(ids.begin(), ids.end());
    return ids;
}

Result<LoadedDevice> loadShippedDevice(std::string_view id) {
    const auto dir = deviceRoot() / std::string(id);
    if (!std::filesystem::exists(dir / "device.json")) {
        Error e(ErrorCode::Hardware_ + 7, std::format("no shipped device '{}'", id));
        auto ids = shippedDeviceIds();
        std::string list;
        for (auto& s : ids)
            list += (list.empty() ? "" : ", ") + s;
        e.notes.push_back("available: " + list);
        return std::unexpected(std::move(e));
    }
    return loadDevice(dir);
}

} // namespace qlab::hw
