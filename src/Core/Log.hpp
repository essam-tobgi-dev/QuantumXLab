#pragma once
#include <cstddef>
#include <string>
#include <vector>
// Spec 04 §3 — logging categories over spdlog.
#include <format>
#include <string_view>
namespace qlab::core {
enum class LogLevel { Trace, Debug, Info, Warn, Error };
enum class LogCat { Core, Sim, Compiler, Hardware, Cryo, Instr, Gfx, Ui, App, Test };
void logInit(LogLevel minimum = LogLevel::Info);
void logRaw(LogCat cat, LogLevel lvl, std::string_view msg);
std::string_view catName(LogCat c);

template <class... Args>
void log(LogCat cat, LogLevel lvl, std::format_string<Args...> fmt, Args&&... args) {
    logRaw(cat, lvl, std::format(fmt, std::forward<Args>(args)...));
}
#define QXL_LOG_INFO(cat, ...)                                                                     \
    ::qlab::core::log(::qlab::core::LogCat::cat, ::qlab::core::LogLevel::Info, __VA_ARGS__)
#define QXL_LOG_WARN(cat, ...)                                                                     \
    ::qlab::core::log(::qlab::core::LogCat::cat, ::qlab::core::LogLevel::Warn, __VA_ARGS__)
#define QXL_LOG_ERROR(cat, ...)                                                                    \
    ::qlab::core::log(::qlab::core::LogCat::cat, ::qlab::core::LogLevel::Error, __VA_ARGS__)
#define QXL_LOG_DEBUG(cat, ...)                                                                    \
    ::qlab::core::log(::qlab::core::LogCat::cat, ::qlab::core::LogLevel::Debug, __VA_ARGS__)

// Ring buffer of recent messages for the in-app Log panel (spec 19).
struct LogEntry {
    LogCat cat;
    LogLevel lvl;
    std::string text;
    double timeS;
};
std::vector<LogEntry> recentLog(std::size_t max = 500);
} // namespace qlab::core
