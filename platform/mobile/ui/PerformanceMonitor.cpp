#include "PerformanceMonitor.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>

extern "C" float port_get_game_speed(void);

namespace mobile_ui {

PerformanceMonitor::PerformanceMonitor() = default;

void PerformanceMonitor::reportFramePresented() {
    mFpsFrameCounter++;
}

void PerformanceMonitor::reportGpuRenderTime(int64_t renderDurationNs) {
    if (renderDurationNs > 0) {
        mGpuRenderTimeAccumNs += renderDurationNs;
        mGpuRenderFrameCount++;
    }
}

void PerformanceMonitor::update(int targetFps, bool batterySaver) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t nowNs = (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
    if (mFpsLastTimeNs == 0) mFpsLastTimeNs = nowNs;
    int64_t diffNs = nowNs - mFpsLastTimeNs;
    if (diffNs < 500000000ll) return; // Sample every 500ms

    float sec = (float)diffNs / 1e9f;
    mCurrentFps = (float)mFpsFrameCounter / sec;
    int activeTargetFps = batterySaver ? 30 : targetFps;
    if (port_get_game_speed() <= 1.05f && mCurrentFps > (float)activeTargetFps) {
        mCurrentFps = (float)activeTargetFps;
    }
    mAvgFrameTimeMs = 1000.0f / (mCurrentFps > 1.0f ? mCurrentFps : 1.0f);
    mFpsFrameCounter = 0;
    mFpsLastTimeNs = nowNs;

    // 1. Sample CPU usage
    bool gotCpu = false;
    static unsigned long long prevUser = 0, prevNice = 0, prevSystem = 0, prevIdle = 0;
    FILE* fpStat = fopen("/proc/stat", "r");
    if (fpStat) {
        unsigned long long u = 0, n = 0, s = 0, idle = 0, iowait = 0, irq = 0, softirq = 0;
        if (fscanf(fpStat, "cpu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &idle, &iowait, &irq, &softirq) >= 4) {
            unsigned long long total = (u + n + s + idle + iowait + irq + softirq);
            unsigned long long prevTotal = (prevUser + prevNice + prevSystem + prevIdle);
            if (prevTotal > 0 && total > prevTotal) {
                unsigned long long totalDiff = total - prevTotal;
                unsigned long long idleDiff = (idle + iowait) - prevIdle;
                if (totalDiff > 0 && idleDiff <= totalDiff) {
                    mCpuUsagePercent = (float)(totalDiff - idleDiff) * 100.0f / (float)totalDiff;
                    gotCpu = true;
                }
            }
            prevUser = u + n;
            prevSystem = s + irq + softirq;
            prevIdle = idle + iowait;
        }
        fclose(fpStat);
    }

    if (!gotCpu) {
        static unsigned long long prevProcTicks = 0;
        FILE* fpSelf = fopen("/proc/self/stat", "r");
        if (fpSelf) {
            char statBuf[512];
            if (fgets(statBuf, sizeof(statBuf), fpSelf)) {
                char* paren = strrchr(statBuf, ')');
                if (paren) {
                    unsigned long long utime = 0, stime = 0;
                    if (sscanf(paren + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &utime, &stime) == 2) {
                        unsigned long long totalTicks = utime + stime;
                        if (prevProcTicks > 0 && totalTicks >= prevProcTicks && sec > 0.001f) {
                            long clkTck = sysconf(_SC_CLK_TCK);
                            if (clkTck <= 0) clkTck = 100;
                            long numCores = sysconf(_SC_NPROCESSORS_ONLN);
                            if (numCores <= 0) numCores = 1;
                            float cpuSec = (float)(totalTicks - prevProcTicks) / (float)clkTck;
                            float usage = (cpuSec / sec) * 100.0f / (float)numCores;
                            if (usage < 0.0f) usage = 0.0f;
                            if (usage > 100.0f) usage = 100.0f;
                            mCpuUsagePercent = usage;
                        }
                        prevProcTicks = totalTicks;
                    }
                }
            }
            fclose(fpSelf);
        }
    }

    // 2. Sample RAM usage
    FILE* fpStatm = fopen("/proc/self/statm", "r");
    if (fpStatm) {
        unsigned long sizePages = 0, residentPages = 0;
        if (fscanf(fpStatm, "%lu %lu", &sizePages, &residentPages) == 2) {
            mRamUsageMB = (float)(residentPages * 4096) / (1024.0f * 1024.0f);
        }
        fclose(fpStatm);
    }

    // 3. Sample GPU usage
    bool gotGpu = false;
    static const char* const gpuSysfsPaths[] = {
        "/sys/kernel/ged/hal/gpu_utilization",
        "/sys/class/misc/mali0/device/utilization",
        "/sys/devices/platform/13000000.mali/utilization",
        "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage"
    };
    for (const char* path : gpuSysfsPaths) {
        FILE* fpGpu = fopen(path, "r");
        if (fpGpu) {
            int val = -1;
            if (fscanf(fpGpu, "%d", &val) == 1 && val >= 0) {
                mGpuUsagePercent = (float)(val > 100 ? 100 : val);
                gotGpu = true;
            }
            fclose(fpGpu);
            if (gotGpu) break;
        }
    }
    if (!gotGpu) {
        if (diffNs > 0 && mGpuRenderFrameCount > 0) {
            float gpuDuty = ((float)mGpuRenderTimeAccumNs / (float)diffNs) * 100.0f;
            if (gpuDuty < 0.0f) gpuDuty = 0.0f;
            if (gpuDuty > 100.0f) gpuDuty = 100.0f;
            mGpuUsagePercent = gpuDuty;
        } else if (mCurrentFps > 0.0f) {
            mGpuUsagePercent = std::min(100.0f, (mAvgFrameTimeMs / 16.67f) * 40.0f);
        }
    }
    mGpuRenderTimeAccumNs = 0;
    mGpuRenderFrameCount = 0;
}

}  // namespace mobile_ui
