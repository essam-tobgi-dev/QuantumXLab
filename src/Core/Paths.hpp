#pragma once
#include <filesystem>
namespace qlab::core {
std::filesystem::path assetDir();      // QXL_ASSET_DIR, overridable via QXL_ASSETS env var
std::filesystem::path userDataDir();   // platform user_data/QuantumXLab (spec 04 §10)
} // namespace qlab::core
