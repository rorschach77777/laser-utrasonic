#pragma once

#include <QtGlobal>
#include <chrono>

namespace RadarHostClock {

inline qint64 nowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct ClockPair {
    qint64 steadyUs = 0;
    qint64 unixUs = 0;
    qint64 spreadUs = 0;
};

// Take wall clock between two monotonic readings. The midpoint bounds
// conversion jitter; an OS clock adjustment remains a separate uncertainty.
// No external hardware-trigger timing information is provided by this pair.
inline ClockPair samplePair()
{
    const auto a = std::chrono::steady_clock::now();
    const auto utc = std::chrono::system_clock::now();
    const auto b = std::chrono::steady_clock::now();
    const qint64 start = std::chrono::duration_cast<std::chrono::microseconds>(
        a.time_since_epoch()).count();
    const qint64 end = std::chrono::duration_cast<std::chrono::microseconds>(
        b.time_since_epoch()).count();
    return {(start + end) / 2,
            std::chrono::duration_cast<std::chrono::microseconds>(
                utc.time_since_epoch()).count(),
            end - start};
}
}
