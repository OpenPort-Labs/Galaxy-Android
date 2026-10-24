#include "FramePacer.h"
#include "FramePacerTiming.h"
#include <sched.h>
#include <time.h>
#include "port/port.h"
#include "../system/SystemManager.h"

namespace mobile_display {

void FramePacer::sync(android_app* app, int targetFps, float gameSpeed) {
    (void)app;
    (void)gameSpeed;
    // Presentation cadence is locked to display target refresh rate.
    // Game simulation speed (gameSpeed multiplier) drives physics/tick rates,
    // NOT the display presentation cadence.
    int effectiveFps = pacing::clampTargetFps(targetFps);
    int64_t framePeriodNs = pacing::framePeriodNs(effectiveFps);

    int64_t nowNs = port_host_time_ns();
    if (pacing::phaseLost(nowNs, mLastPresentNs, framePeriodNs)) {
        mLastPresentNs = nowNs;
        return;
    }

    int64_t lastNs = mLastPresentNs;
    int64_t targetNs = pacing::nextDeadline(lastNs, framePeriodNs);
    if (nowNs < targetNs) {
        int64_t waitNs = targetNs - nowNs;
        int64_t sleepRecordedNs = 0;
        if (waitNs > 1500000ll) {
            int64_t coarseSleepNs = waitNs - 500000ll;
            struct timespec req;
            req.tv_sec = coarseSleepNs / 1000000000ll;
            req.tv_nsec = coarseSleepNs % 1000000000ll;
            int64_t sleepStartNs = port_host_time_ns();
            nanosleep(&req, nullptr);
            sleepRecordedNs = port_host_time_ns() - sleepStartNs;
        }

        while (true) {
            int64_t rem = targetNs - port_host_time_ns();
            if (rem <= 0) break;
            if (rem > 250000ll) {
                struct timespec req;
                req.tv_sec = 0;
                req.tv_nsec = rem - 100000ll;
                nanosleep(&req, nullptr);
            } else {
                sched_yield();
                if (targetNs - port_host_time_ns() > 20000ll) {
                    struct timespec shortReq{0, 20000ll};
                    nanosleep(&shortReq, nullptr);
                }
                break;
            }
        }

        if (sleepRecordedNs > 0) {
            port_perf_pace(sleepRecordedNs);
        }
    }
    // Anchor on-time frames to the exact deadline and missed ones to now,
    // so VSync phase cannot drift or accumulate negative debt.
    mLastPresentNs = pacing::settlePhase(nowNs, lastNs, framePeriodNs);
}

bool FramePacer::shouldPresentSlot(int targetFps, float gameSpeed, int64_t nowNs) const {
    (void)gameSpeed;
    return pacing::presentSlotDue(nowNs, mLastPresentNs, targetFps);
}

void FramePacer::reset() {
    mLastPresentNs = 0;
}

}  // namespace mobile_display
