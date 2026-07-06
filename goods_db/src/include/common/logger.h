#pragma once

#include <chrono>
#include <string>

#include <fmt/format.h>

namespace goods_db {

/**
 * Logging macros and utilities.
 * Uses fmt library for formatting.
 */

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

/** Set the global log level. Messages below this level are suppressed. */
void SetLogLevel(LogLevel level);

/** Get current log level */
LogLevel GetLogLevel();

/** Get current timestamp string for logging */
std::string GetTimestamp();

/** Core log function - internal use */
void Log(LogLevel level, const char* file, int line, const char* func,
         const std::string& message);

}  // namespace goods_db

// =============================================================================
// Logging Macros
// =============================================================================

#define GOODS_DB_LOG(level, fmt_str, ...)                                      \
    do {                                                                       \
        if (::goods_db::GetLogLevel() <= (level)) {                            \
            ::goods_db::Log((level), __FILE__, __LINE__, __func__,             \
                            fmt::format(fmt_str, ##__VA_ARGS__));              \
        }                                                                      \
    } while (0)

#define LOG_DEBUG(fmt_str, ...) \
    GOODS_DB_LOG(::goods_db::LogLevel::DEBUG, fmt_str, ##__VA_ARGS__)
#define LOG_INFO(fmt_str, ...) \
    GOODS_DB_LOG(::goods_db::LogLevel::INFO, fmt_str, ##__VA_ARGS__)
#define LOG_WARN(fmt_str, ...) \
    GOODS_DB_LOG(::goods_db::LogLevel::WARN, fmt_str, ##__VA_ARGS__)
#define LOG_ERROR(fmt_str, ...) \
    GOODS_DB_LOG(::goods_db::LogLevel::ERROR, fmt_str, ##__VA_ARGS__)

// =============================================================================
// Assertion Macro
// =============================================================================

#define GOODS_DB_ASSERT(cond, msg)                                             \
    do {                                                                       \
        if (!(cond)) {                                                         \
            LOG_ERROR("ASSERTION FAILED: {} at {}:{} in {}", msg, __FILE__,    \
                      __LINE__, __func__);                                     \
            std::abort();                                                      \
        }                                                                      \
    } while (0)
