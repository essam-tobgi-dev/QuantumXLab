#include "QSim/Backend.hpp"
#include <cmath>
#ifdef __APPLE__
#include <sys/sysctl.h>
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#include <string>
#endif
namespace qlab::qsim {

std::size_t availableMemoryBytes() {
    constexpr std::size_t reserve = 512ull << 20;
    std::size_t avail = 0;
#ifdef __APPLE__
    std::uint64_t total = 0; std::size_t len = sizeof(total);
    sysctlbyname("hw.memsize", &total, &len, nullptr, 0);
    vm_size_t page = 0; host_page_size(mach_host_self(), &page);
    vm_statistics64_data_t vm{}; mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &cnt) == KERN_SUCCESS)
        avail = static_cast<std::size_t>(vm.free_count + vm.inactive_count + vm.purgeable_count) * page;
    if (avail == 0 || avail > total) avail = static_cast<std::size_t>(total) / 2;
#elif defined(__linux__)
    std::ifstream f("/proc/meminfo"); std::string k; std::size_t v; std::string unit;
    while (f >> k >> v >> unit) if (k == "MemAvailable:") { avail = v * 1024; break; }
    if (avail == 0) avail = 4ull << 30;
#else
    avail = 4ull << 30;
#endif
    return avail > reserve ? avail - reserve : 0;
}

std::uint32_t maxQubitsFor(std::size_t bytesPerEntry, bool squared) {
    double budget = 0.8 * static_cast<double>(availableMemoryBytes());
    double entries = budget / static_cast<double>(bytesPerEntry);
    if (entries < 2) return 0;
    double n = std::log2(entries);
    if (squared) n /= 2.0;
    return static_cast<std::uint32_t>(std::floor(n));
}

} // namespace qlab::qsim
