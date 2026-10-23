#include "FramePacer.h"
#include <sched.h>
#include <time.h>
#include "port/port.h"
#include "../system/SystemManager.h"

namespace mobile_display {

void FramePacer::sync(android_app* app, int targetFps, float gameSpeed) {
    (void)app;
    (void)gameSpeed;
    if (targetFps < 30) targetFps = 30;
    if (targetFps > 144) targetFps = 144;

    // Presentation cadence is locked to display target refresh rate.
    // Game simulation speed (gameSpeed multiplier) drives physics/tick rates,
    // NOT the display presentation cadence.
    int effectiveFps = targetFps;
    int64_t framePeriodNs = 1000000000ll / (effectiveFps > 0 ? effectiveFps : 60);

    int64_t nowNs = port_host_time_ns();
    if (mLastPresentNs == 0 || nowNs < mLastPresentNs || (nowNs - mLastPresentNs > framePeriodNs * 2)) {
        mLastPresentNs = nowNs;
        return;
    }

    int64_t targetNs = mLastPresentNs + framePeriodNs;
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
        // Advance target timestamp monotonically to eliminate VSync drift and beating
        mLastPresentNs = targetNs;
    } else {
        // Target deadline was already missed. Reset mLastPresentNs to nowNs immediately
        // so future frames do not inherit negative phase debt.
        mLastPresentNs = nowNs;
    }
}

bool FramePacer::shouldPresentSlot(int targetFps, float gameSpeed, int64_t nowNs) const {
    (void)gameSpeed;
    if (targetFps <= 60) return false;
    int effectiveFps = targetFps;
    int64_t framePeriodNs = 1000000000ll / (effectiveFps > 0 ? effectiveFps : 60);
    return (nowNs - mLastPresentNs >= framePeriodNs);
}

void FramePacer::reset() {
    mLastPresentNs = 0;
}

}  // namespace mobile_display
