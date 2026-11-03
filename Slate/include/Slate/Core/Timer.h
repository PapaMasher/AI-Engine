#pragma once

#include "Slate/Core/Base.h"

#include <chrono>

namespace Slate {

// Measures wall-clock time for profiling sections of code.
class Timer
{
public:
    Timer() { Reset(); }

    void Reset() { m_Start = std::chrono::steady_clock::now(); }

    double ElapsedSeconds() const
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_Start).count();
    }

    double ElapsedMilliseconds() const { return ElapsedSeconds() * 1000.0; }

private:
    std::chrono::steady_clock::time_point m_Start;
};

// Per-frame delta time and smoothed FPS counter.
class FrameTimer
{
public:
    FrameTimer() { Reset(); }

    void Reset()
    {
        m_LastTime = std::chrono::steady_clock::now();
        m_DeltaTime = 0.0;
        m_Accumulator = 0.0;
        m_FrameCount = 0;
        m_FramesPerSecond = 0.0;
    }

    // Call once per frame, before any simulation work.
    void Tick()
    {
        const auto now = std::chrono::steady_clock::now();
        m_DeltaTime = std::chrono::duration<double>(now - m_LastTime).count();
        m_LastTime = now;

        m_Accumulator += m_DeltaTime;
        ++m_FrameCount;
        if (m_Accumulator >= 0.5)
        {
            m_FramesPerSecond = static_cast<double>(m_FrameCount) / m_Accumulator;
            m_Accumulator = 0.0;
            m_FrameCount = 0;
        }
    }

    double GetDeltaTime() const { return m_DeltaTime; }
    double GetFramesPerSecond() const { return m_FramesPerSecond; }

private:
    std::chrono::steady_clock::time_point m_LastTime;
    double m_DeltaTime = 0.0;
    double m_Accumulator = 0.0;
    uint32_t m_FrameCount = 0;
    double m_FramesPerSecond = 0.0;
};

} // namespace Slate
