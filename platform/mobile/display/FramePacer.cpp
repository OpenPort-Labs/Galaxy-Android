#include "FramePacer.h"
#include <sched.h>
#include <time.h>
#include "port/port.h"
#include "../system/SystemManager.h"

namespace mobile_display {

void FramePacer::sync(android_app* app, int targetFps, float gameSpeed) {
    (void)app;
    if (targetFps < 30) targetFps = 30;
    if (targetFps > 144) targetFps = 144;

    int effectiveFps = targetFps;
    if (gameSpeed > 1.05f) {
        effectiveFps = (int)((float)targetFps * gameSpeed);
    }
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
            } else if (rem > 50000ll) {
                sched_yield();
            } else {
#if defined(__aarch64__) || defined(__arm__)
                asm volatile("yield" ::: "memory");
#endif
            }
        }

        if (sleepRecordedNs > 0) {
            port_perf_pace(sleepRecordedNs);
        }
    }

    nowNs = port_host_time_ns();
    if (nowNs - mLastPresentNs > framePeriodNs + framePeriodNs / 2) {
        mLastPresentNs = nowNs;
    } else {
        mLastPresentNs += framePeriodNs;
    }
}

bool FramePacer::shouldPresentSlot(int targetFps, float gameSpeed, int64_t nowNs) const {
    if (targetFps <= 60) return false;
    int effectiveFps = (gameSpeed > 1.05f) ? (int)((float)targetFps * gameSpeed) : targetFps;
    int64_t framePeriodNs = 1000000000ll / (effectiveFps > 0 ? effectiveFps : 60);
    return (nowNs - mLastPresentNs >= framePeriodNs);
}

void FramePacer::reset() {
    mLastPresentNs = 0;
}

}  // namespace mobile_display
