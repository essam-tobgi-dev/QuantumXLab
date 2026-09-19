#include "Core/Log.hpp"
#include <chrono>
#include <deque>
#include <mutex>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace qlab::core {
namespace {
std::mutex g_mu;
std::deque<LogEntry> g_ring;
auto g_t0 = std::chrono::steady_clock::now();
spdlog::level::level_enum toSpd(LogLevel l) {
    switch (l) {
    case LogLevel::Trace:
        return spdlog::level::trace;
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warn:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    }
    return spdlog::level::info;
}
} // namespace

std::string_view catName(LogCat c) {
    switch (c) {
    case LogCat::Core:
        return "core";
    case LogCat::Sim:
        return "sim";
    case LogCat::Compiler:
        return "compiler";
    case LogCat::Hardware:
        return "hw";
    case LogCat::Cryo:
        return "cryo";
    case LogCat::Instr:
        return "instr";
    case LogCat::Gfx:
        return "gfx";
    case LogCat::Ui:
        return "ui";
    case LogCat::App:
        return "app";
    case LogCat::Test:
        return "test";
    }
    return "?";
}

namespace {
// Create the sink once, without touching the level: only logInit() sets the level, so a caller
// that raised or lowered it is never overridden by a later message (found by the Noise module).
void ensureLogger() {
    static std::once_flag once;
    std::call_once(once, [] {
        auto logger = spdlog::stdout_color_mt("qxl");
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
        spdlog::set_level(spdlog::level::info); // default until logInit() says otherwise
    });
}
} // namespace

void logInit(LogLevel minimum) {
    ensureLogger();
    spdlog::set_level(toSpd(minimum));
}

void logRaw(LogCat cat, LogLevel lvl, std::string_view msg) {
    ensureLogger();
    spdlog::log(toSpd(lvl), "[{}] {}", catName(cat), msg);
    std::lock_guard lk(g_mu);
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count();
    g_ring.push_back({cat, lvl, std::string(msg), t});
    if (g_ring.size() > 2000)
        g_ring.pop_front();
}

std::vector<LogEntry> recentLog(std::size_t max) {
    std::lock_guard lk(g_mu);
    std::size_t n = std::min(max, g_ring.size());
    return std::vector<LogEntry>(g_ring.end() - static_cast<std::ptrdiff_t>(n), g_ring.end());
}
} // namespace qlab::core
