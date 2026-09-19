#include "Core/Paths.hpp"
#include <cstdlib>
namespace qlab::core {
std::filesystem::path assetDir() {
    if (const char* e = std::getenv("QXL_ASSETS"))
        return e;
    return QXL_ASSET_DIR;
}
std::filesystem::path userDataDir() {
#ifdef __APPLE__
    if (const char* h = std::getenv("HOME"))
        return std::filesystem::path(h) / "Library/Application Support/QuantumXLab";
#elif defined(_WIN32)
    if (const char* a = std::getenv("APPDATA"))
        return std::filesystem::path(a) / "QuantumXLab";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"))
        return std::filesystem::path(x) / "QuantumXLab";
    if (const char* h = std::getenv("HOME"))
        return std::filesystem::path(h) / ".local/share/QuantumXLab";
#endif
    return std::filesystem::temp_directory_path() / "QuantumXLab";
}
} // namespace qlab::core
