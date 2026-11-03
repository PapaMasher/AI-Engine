#include "Slate/Core/Log.h"

#include <chrono>
#include <ctime>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Slate {

namespace {

const char* LevelToString(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace:    return "TRACE";
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warn:     return "WARN";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
        default:                 return "OFF";
    }
}

const char* LevelToColor(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace:    return "\x1b[90m";
        case LogLevel::Debug:    return "\x1b[36m";
        case LogLevel::Info:     return "\x1b[37m";
        case LogLevel::Warn:     return "\x1b[33m";
        case LogLevel::Error:    return "\x1b[31m";
        case LogLevel::Critical: return "\x1b[91m";
        default:                 return "";
    }
}

bool s_ColorEnabled = true;

void EnableVirtualTerminal()
{
#if defined(_WIN32)
    HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    if (handle == INVALID_HANDLE_VALUE)
        return;

    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode))
    {
        s_ColorEnabled = false;
        return;
    }

    if (!SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        s_ColorEnabled = false;
#endif
}

std::string Timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;

    std::tm local {};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d",
        local.tm_hour, local.tm_min, local.tm_sec, static_cast<int>(millis));
    return buffer;
}

} // namespace

void Log::Init(const std::string& logFilePath)
{
    std::lock_guard lock(s_Mutex);
    if (s_Initialized)
        return;

    EnableVirtualTerminal();

    if (!logFilePath.empty())
    {
        s_FileStream.open(logFilePath, std::ios::out | std::ios::trunc);
        if (!s_FileStream.is_open())
        {
            std::fprintf(stderr, "[Slate] Failed to open log file '%s'\n", logFilePath.c_str());
        }
    }

    s_Initialized = true;
}

void Log::Shutdown()
{
    std::lock_guard lock(s_Mutex);
    if (s_FileStream.is_open())
        s_FileStream.close();
    s_Initialized = false;
}

void Log::Write(LogLevel level, std::string_view message)
{
    std::lock_guard lock(s_Mutex);

    const std::string timestamp = Timestamp();
    const char* levelName = LevelToString(level);

    if (s_ColorEnabled)
    {
        std::fprintf(stderr, "%s[%s] [%s]\x1b[0m %.*s\n",
            LevelToColor(level), timestamp.c_str(), levelName,
            static_cast<int>(message.size()), message.data());
    }
    else
    {
        std::fprintf(stderr, "[%s] [%s] %.*s\n", timestamp.c_str(), levelName,
            static_cast<int>(message.size()), message.data());
    }

    if (s_FileStream.is_open())
    {
        s_FileStream << "[" << timestamp << "] [" << levelName << "] " << message << '\n';
        s_FileStream.flush();
    }
}

void AssertFailed(const char* expression, std::string_view message)
{
    Log::Critical("Assertion failed: {}\n  {}", expression, message);
#if defined(_WIN32)
    __debugbreak();
#else
    std::abort();
#endif
}

} // namespace Slate
