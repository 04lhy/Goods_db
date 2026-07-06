#include "common/logger.h"
#include <cstdio>
#include <ctime>
#include <mutex>

namespace goods_db {

static LogLevel g_log_level = LogLevel::INFO;
static std::mutex g_log_mutex;

void SetLogLevel(LogLevel level) {
    g_log_level = level;
}

LogLevel GetLogLevel() {
    return g_log_level;
}

std::string GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::tm tm_buf;
    localtime_r(&time, &tm_buf);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                  static_cast<int>(ms.count()));
    return buf;
}

void Log(LogLevel level, const char* file, int line, const char* func,
         const std::string& message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);

    const char* level_str = "UNKNOWN";
    switch (level) {
        case LogLevel::DEBUG: level_str = "DEBUG"; break;
        case LogLevel::INFO:  level_str = "INFO "; break;
        case LogLevel::WARN:  level_str = "WARN "; break;
        case LogLevel::ERROR: level_str = "ERROR"; break;
    }

    std::fprintf(stderr, "[%s] [%s] [%s:%d:%s] %s\n",
                 GetTimestamp().c_str(), level_str, file, line, func,
                 message.c_str());
    std::fflush(stderr);
}

}  // namespace goods_db
