#pragma once
// Umbrella header for qlab::hw (spec 09).
#include "Hardware/Calibration.hpp"
#include "Hardware/Device.hpp"
#include "Hardware/FrequencyPlan.hpp"
#include "Hardware/IonChain.hpp"
#include "Hardware/Loader.hpp"
#include "Hardware/SystemModel.hpp"
#include "Hardware/Transmon.hpp"

namespace qlab::hw {
// Directory holding the shipped devices (Assets/Devices), honouring QXL_ASSETS.
std::filesystem::path deviceRoot();
// Loads a shipped device by id (spec 09 §4): sc_fixed_5, sc_heavyhex_27, sc_heavyhex_127,
// sc_tunable_grid_54, ion_chain_11, ion_chain_32.
Result<LoadedDevice> loadShippedDevice(std::string_view id);
std::vector<std::string> shippedDeviceIds();
} // namespace qlab::hw
