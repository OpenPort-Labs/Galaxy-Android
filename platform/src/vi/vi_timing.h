#pragma once

#include <stdint.h>

// Pure retrace-cadence decisions shared by the VI clock and its host tests.
// Kept free of OS/host calls so the tests exercise the shipped thresholds.
namespace port_vi_timing {

// NTSC / EURGB60 base field period (1001/60000 s = ~16.683350 ms).
inline constexpr int64_t kBaseViPeriodNs = 16683350LL;

inline int64_t simulationPeriodNs(float speedMultiplier) {
    float speed = speedMultiplier;
    if (speed < 0.1f) speed = 1.0f;
    return (int64_t)((double)kBaseViPeriodNs / speed);
}

// The simulation never runs faster than the Wii field rate: an external
// (display-timed) retrace may satisfy a cadence window no earlier than 88%
// of the period, so 90/120/144 Hz presentation cannot accelerate the game.
inline int64_t minRetraceIntervalNs(int64_t simPeriodNs) {
    int64_t minInterval = (int64_t)(simPeriodNs * 0.88);
    if (minInterval < 2000000LL) minInterval = 2000000LL;
    return minInterval;
}

inline bool retraceAllowed(int64_t nowNs, int64_t lastRaisedNs, int64_t simPeriodNs) {
    return nowNs - lastRaisedNs >= minRetraceIntervalNs(simPeriodNs);
}

// The fallback clock fills the slots a slower presentation rate leaves open,
// but skips a tick when a display retrace already served the window.
inline bool fallbackTickDue(int64_t nowNs, int64_t lastRaisedNs, int64_t simPeriodNs) {
    return nowNs - lastRaisedNs >= (int64_t)(simPeriodNs * 0.70);
}

// Advance the clock deadline by one period, resyncing after a stall instead
// of bursting the missed ticks back to back.
inline int64_t advanceDeadline(int64_t nextNs, int64_t nowNs, int64_t periodNs) {
    nextNs += periodNs;
    if (nowNs > nextNs + periodNs * 2) {
        nextNs = nowNs + periodNs;
    }
    return nextNs;
}

}  // namespace port_vi_timing
