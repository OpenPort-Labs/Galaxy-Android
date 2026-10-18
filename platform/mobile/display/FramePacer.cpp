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

    if (mCurrentRefreshRate != targetFps) {
        mCurrentRefreshRate = targetFps;
        port_set_display_refresh_rate(targetFps);
    }

    int effectiveFps = targetFps;
    if (gameSpeed > 1.05f) {
        effectiveFps = (int)((float)targetFps * gameSpeed);
    }
    int64_t framePeriodNs = 1000000000ll / (effectiveFps > 0 ? effectiveFps : 60);

    int64_t nowNs = port_host_time_ns();
    if (mLastPresentNs == 0 || (nowNs - mLastPresentNs > framePeriodNs * 4)) {
        mLastPresentNs = nowNs;
        return;
    }

    int64_t targetNs = mLastPresentNs + framePeriodNs;
    if (nowNs < targetNs) {
        int64_t waitNs = targetNs - nowNs;
        if (waitNs > 2000000ll) {
            struct timespec req;
            int64_t sleepNs = waitNs - 1500000ll;
            req.tv_sec = sleepNs / 1000000000ll;
            req.tv_nsec = sleepNs % 1000000000ll;
            int64_t sleepStartNs = port_host_time_ns();
            nanosleep(&req, nullptr);
            port_perf_pace(port_host_time_ns() - sleepStartNs);
        }
        while (port_host_time_ns() < targetNs) {
            sched_yield();
        }
    }

    nowNs = port_host_time_ns();
    if (nowNs > targetNs + framePeriodNs / 2) {
        mLastPresentNs = nowNs;
    } else {
        mLastPresentNs = targetNs;
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
