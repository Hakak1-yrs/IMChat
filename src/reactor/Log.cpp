#include "Log.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace {

std::atomic<Log::Level> g_level{ Log::Level::INFO };
std::mutex g_mutex;

const char* levelName(Log::Level level) {
    switch (level) {
    case Log::Level::DEBUG: return "DEBUG";
    case Log::Level::INFO: return "INFO ";
    case Log::Level::WARN: return "WARN ";
    case Log::Level::ERROR_: return "ERROR";
    }
    return "?";
}

}  // namespace

void Log::setLevel(Level level) { g_level = level; }

void Log::debug(const std::string& text) { write(Level::DEBUG, text); }

void Log::info(const std::string& text) { write(Level::INFO, text); }

void Log::warn(const std::string& text) { write(Level::WARN, text); }

void Log::error(const std::string& text) { write(Level::ERROR_, text); }

void Log::write(Level level, const std::string& text) {
    if (level < g_level.load()) return;
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const long long millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
    std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(stderr, "%s.%03lld %s %s\n", stamp, millis, levelName(level), text.c_str());
}
