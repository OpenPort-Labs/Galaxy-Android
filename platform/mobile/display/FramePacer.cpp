#include "FramePacer.h"

#include <time.h>
#include "port/port.h"
#include "../system/SystemManager.h"

namespace mobile_display {

void FramePacer::sync(android_app* app, int targetFps, float gameSpeed) {
    if (targetFps < 30) targetFps = 30;
    if (targetFps > 144) targetFps = 144;

    if (mCurrentRefreshRate != targetFps) {
        mCurrentRefreshRate = targetFps;
        mobile_system::SystemManager::setPreferredRefreshRate(app, (float)targetFps);
        port_set_display_refresh_rate(targetFps);
    }

    if (gameSpeed > 1.05f) {
        targetFps = (int)((float)targetFps * gameSpeed);
    }
    int64_t framePeriodNs = 1000000000ll / targetFps;

    int64_t nowNs = port_host_time_ns();
    if (mLastPresentNs != 0) {
        int64_t elapsedNs = nowNs - mLastPresentNs;
        if (elapsedNs < framePeriodNs) {
            int64_t waitNs = framePeriodNs - elapsedNs;
            // 1.2 ms margin prevents Android kernel timer slack from missing VSync
            if (waitNs > 1500000ll) {
                struct timespec req;
                int64_t sleepNs = waitNs - 1200000ll;
                req.tv_sec = sleepNs / 1000000000ll;
                req.tv_nsec = sleepNs % 1000000000ll;
                int64_t sleepStartNs = port_host_time_ns();
                nanosleep(&req, nullptr);
                port_perf_pace(port_host_time_ns() - sleepStartNs);
            }
        }
    }

    mLastPresentNs = port_host_time_ns();
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
