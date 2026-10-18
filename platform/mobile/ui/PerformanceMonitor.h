#pragma once

#include <stdint.h>
#include <string>

namespace mobile_ui {

class PerformanceMonitor {
public:
    PerformanceMonitor();

    // Call when a frame is presented to track frame counter
    void reportFramePresented();

    // Call after GPU rendering pass to track active duty cycle
    void reportGpuRenderTime(int64_t renderDurationNs);

    // Periodic telemetry update
    void update(int targetFps, bool batterySaver);

    float currentFps() const { return mCurrentFps; }
    float avgFrameTimeMs() const { return mAvgFrameTimeMs; }
    float cpuUsagePercent() const { return mCpuUsagePercent; }
    float gpuUsagePercent() const { return mGpuUsagePercent; }
    float ramUsageMB() const { return mRamUsageMB; }
    bool hasHardwareGpuUsage() const { return mHasHardwareGpuUsage; }

    const std::string& gpuRendererName() const { return mGpuRendererName; }
    void setGpuRendererName(const std::string& name) {
        if (!name.empty()) mGpuRendererName = name;
    }

private:
    float mCurrentFps = 60.0f;
    float mAvgFrameTimeMs = 16.6f;
    int mFpsFrameCounter = 0;
    int64_t mFpsLastTimeNs = 0;

    float mCpuUsagePercent = 0.0f;
    float mGpuUsagePercent = 0.0f;
    bool mHasHardwareGpuUsage = false;
    float mRamUsageMB = 0.0f;

    int64_t mGpuRenderTimeAccumNs = 0;
    int mGpuRenderFrameCount = 0;

    std::string mGpuRendererName = "Mali-G68 GPU";
};

}  // namespace mobile_ui
