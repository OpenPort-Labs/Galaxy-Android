#pragma once

#include <stdint.h>

// Pure pacing decisions shared by FramePacer and its host tests.
namespace mobile_display {
namespace pacing {

inline constexpr int kMinTargetFps = 30;
inline constexpr int kMaxTargetFps = 144;

inline int clampTargetFps(int fps) {
    if (fps < kMinTargetFps) return kMinTargetFps;
    if (fps > kMaxTargetFps) return kMaxTargetFps;
    return fps;
}

inline int64_t framePeriodNs(int fps) {
    return 1000000000ll / (fps > 0 ? fps : 60);
}

// A zero, backwards or multi-frame-forward timestamp has lost the VSync phase.
inline bool phaseLost(int64_t nowNs, int64_t lastPresentNs, int64_t framePeriodNs) {
    return lastPresentNs == 0 || nowNs < lastPresentNs ||
           (nowNs - lastPresentNs > framePeriodNs * 2);
}

inline int64_t nextDeadline(int64_t lastPresentNs, int64_t framePeriodNs) {
    return lastPresentNs + framePeriodNs;
}

// An on-time frame anchors to the exact deadline so error cannot accumulate;
// a missed deadline re-anchors to now so later frames inherit no phase debt.
inline int64_t settlePhase(int64_t nowNs, int64_t lastPresentNs, int64_t framePeriodNs) {
    int64_t targetNs = nextDeadline(lastPresentNs, framePeriodNs);
    return nowNs < targetNs ? targetNs : nowNs;
}

// Catch-up presentation slots only exist above 60 Hz.
inline bool presentSlotDue(int64_t nowNs, int64_t lastPresentNs, int targetFps) {
    if (targetFps <= 60) return false;
    return nowNs - lastPresentNs >= framePeriodNs(targetFps);
}

}  // namespace pacing
}  // namespace mobile_display
