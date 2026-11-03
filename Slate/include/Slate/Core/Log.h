#pragma once

// Thread-safe logging facility with console and file sinks. The log level can
// be changed at runtime (the editor exposes it in the console panel).

#include "Slate/Core/Base.h"

#include <format>
#include <mutex>
#include <fstream>
#include <cstdio>

namespace Slate {

enum class LogLevel : uint8_t
{
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off
};

class Log
{
public:
    static void Init(const std::string& logFilePath = "Slate.log");
    static void Shutdown();

    static void SetLevel(LogLevel level) { s_Level = level; }
    static LogLevel GetLevel() { return s_Level; }

    static void Write(LogLevel level, std::string_view message);

    template<typename... Args>
    static void Trace(std::format_string<Args...> format, Args&&... args)
    {
        if (s_Level <= LogLevel::Trace)
            Write(LogLevel::Trace, std::format(format, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void Debug(std::format_string<Args...> format, Args&&... args)
    {
        if (s_Level <= LogLevel::Debug)
            Write(LogLevel::Debug, std::format(format, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void Info(std::format_string<Args...> format, Args&&... args)
    {
        if (s_Level <= LogLevel::Info)
            Write(LogLevel::Info, std::format(format, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void Warn(std::format_string<Args...> format, Args&&... args)
    {
        if (s_Level <= LogLevel::Warn)
            Write(LogLevel::Warn, std::format(format, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void Error(std::format_string<Args...> format, Args&&... args)
    {
        if (s_Level <= LogLevel::Error)
            Write(LogLevel::Error, std::format(format, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void Critical(std::format_string<Args...> format, Args&&... args)
    {
        if (s_Level <= LogLevel::Critical)
            Write(LogLevel::Critical, std::format(format, std::forward<Args>(args)...));
    }

private:
    static inline LogLevel s_Level = LogLevel::Info;
    static inline std::mutex s_Mutex;
    static inline std::ofstream s_FileStream;
    static inline bool s_Initialized = false;
};

// Assertion used for programmer errors that must never happen in production.
// In release builds the condition is still evaluated once, so side effects in
// the expression are preserved, but no message is printed.
void AssertFailed(const char* expression, std::string_view message);

#ifdef SLATE_DEBUG_BUILD
#define SLATE_ASSERT(condition, ...)                                              \
    do {                                                                          \
        if (!(condition))                                                         \
            ::Slate::AssertFailed(#condition, ::std::format(__VA_ARGS__));        \
    } while (0)
#else
#define SLATE_ASSERT(condition, ...)                                              \
    do {                                                                          \
        (void)sizeof(condition);                                                  \
        if (!(condition))                                                         \
            ::Slate::AssertFailed(#condition, "assertion failed");                \
    } while (0)
#endif

#define SLATE_TRACE(...)    ::Slate::Log::Trace(__VA_ARGS__)
#define SLATE_DEBUG(...)    ::Slate::Log::Debug(__VA_ARGS__)
#define SLATE_INFO(...)     ::Slate::Log::Info(__VA_ARGS__)
#define SLATE_WARN(...)     ::Slate::Log::Warn(__VA_ARGS__)
#define SLATE_ERROR(...)    ::Slate::Log::Error(__VA_ARGS__)
#define SLATE_CRITICAL(...) ::Slate::Log::Critical(__VA_ARGS__)

} // namespace Slate
