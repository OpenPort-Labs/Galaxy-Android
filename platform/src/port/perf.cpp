// Frame-time statistics for the game thread and the renderer, logged every
// 10 s: always with PETARI_PERFLOG=1, otherwise only while the game runs
// below 58 fps (so the headset's log shows slowdowns).  On Android an app
// process has no environment to set, so `adb shell setprop debug.petari.perflog 1`
// turns the same logging on for field diagnostics.
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#include "port/port.h"

namespace {

struct Stat {
    double sum = 0.0, max = 0.0;
    unsigned count = 0;
    void add(double v) {
        sum += v;
        max = v > max ? v : max;
        count++;
    }
    double avg() const { return count ? sum / count : 0.0; }
};

std::mutex sLock;
Stat sWork, sRecord, sPrepare, sRenderCpu, sUpload, sGpuWait, sUpdate, sPresent, sPace;
Stat sShaderCompile, sPipelineCreate, sQueueWait;
unsigned sFrames = 0, sRenderFrames = 0;
int64_t sFrameStart = 0, sRecordThisFrame = 0, sWindowStart = 0;
int64_t sLastFrameEnd = 0;

// Rolling window for percentiles (last 64 frames)
constexpr size_t kRollingWindowSize = 64;
double sRollingIntervals[kRollingWindowSize] = {0.0};
size_t sRollingIdx = 0;
size_t sRollingCount = 0;

// Sustained slowdown tracking
char sCurrentScene[64] = "unknown";
bool sMenuOpen = false;
unsigned sConsecutiveSlowFrames = 0;
bool sSlowdownReported = false;

bool alwaysLog() {
    if (getenv("PETARI_PERFLOG") != nullptr) return true;
#if defined(__ANDROID__)
    char value[PROP_VALUE_MAX] = {0};
    if (__system_property_get("debug.petari.perflog", value) > 0 && value[0] == '1') return true;
    if (__system_property_get("persist.petari.perflog", value) > 0 && value[0] == '1') return true;
#endif
    return false;
}

void computePercentiles(double& p50, double& p95, double& p99) {
    if (sRollingCount == 0) {
        p50 = p95 = p99 = 0.0;
        return;
    }
    std::vector<double> sorted(sRollingIntervals, sRollingIntervals + sRollingCount);
    std::sort(sorted.begin(), sorted.end());
    p50 = sorted[(size_t)(sorted.size() * 0.50)];
    p95 = sorted[(size_t)(std::min((double)sorted.size() - 1, sorted.size() * 0.95))];
    p99 = sorted[(size_t)(std::min((double)sorted.size() - 1, sorted.size() * 0.99))];
}

void report(int64_t now) {
    double secs = (now - sWindowStart) / 1e9;
    double fps = secs > 0.0 ? sFrames / secs : 0.0;
    double p50 = 0.0, p95 = 0.0, p99 = 0.0;
    computePercentiles(p50, p95, p99);

    if (alwaysLog() || fps < 58.0) {
        port_log("perf: game %.1f fps, intervals [p50=%.1fms, p95=%.1fms, p99=%.1fms], work %.1f ms avg %.1f max (GX recording %.1f avg); renderer %.1f fps, cpu %.1f ms avg %.1f max, "
                 "upload %.1f avg, prepare %.1f avg %.1f max",
                 fps, p50, p95, p99, sWork.avg(), sWork.max, sRecord.avg(), secs > 0.0 ? sRenderFrames / secs : 0.0, sRenderCpu.avg(), sRenderCpu.max, sUpload.avg(), sPrepare.avg(),
                 sPrepare.max);
        if (sGpuWait.count || sQueueWait.count) {
            port_log("perf: GPU wait %.1f ms avg %.1f max; queue wait %.1f ms avg %.1f max",
                     sGpuWait.avg(), sGpuWait.max, sQueueWait.avg(), sQueueWait.max);
        }
        if (sShaderCompile.count || sPipelineCreate.count) {
            port_log("perf: shader compile %u calls (%.1f ms avg %.1f max), pipeline create %u calls (%.1f ms avg %.1f max)",
                     sShaderCompile.count, sShaderCompile.avg(), sShaderCompile.max,
                     sPipelineCreate.count, sPipelineCreate.avg(), sPipelineCreate.max);
        }
        if (sPresent.count || sUpdate.count || sPace.count) {
            port_log("perf: render thread - update %.1f avg %.1f max, present %.1f avg %.1f max, pacer slept %.1f avg %.1f max",
                     sUpdate.avg(), sUpdate.max, sPresent.avg(), sPresent.max, sPace.avg(), sPace.max);
        }
    }
    sWork = sRecord = sPrepare = sRenderCpu = sUpload = sGpuWait = Stat();
    sUpdate = sPresent = sPace = sShaderCompile = sPipelineCreate = sQueueWait = Stat();
    sFrames = sRenderFrames = 0;
    sWindowStart = now;
}

}  // namespace

extern "C" {

void port_perf_frame_begin(void) {
    std::lock_guard<std::mutex> lock(sLock);
    int64_t now = port_host_time_ns();
    if (!sWindowStart) sWindowStart = now;
    if (sLastFrameEnd > 0) {
        double frameIntervalMs = (now - sLastFrameEnd) / 1e6;
        sRollingIntervals[sRollingIdx] = frameIntervalMs;
        sRollingIdx = (sRollingIdx + 1) % kRollingWindowSize;
        if (sRollingCount < kRollingWindowSize) sRollingCount++;

        // Sustained slowdown detection threshold (33.3ms ~ 30 FPS or below)
        if (frameIntervalMs > 33.3) {
            sConsecutiveSlowFrames++;
            if (sConsecutiveSlowFrames >= 30 && !sSlowdownReported) {
                sSlowdownReported = true;
                port_log("perf DIAGNOSTIC ALERT: sustained slowdown detected (%u consecutive frames > 33.3ms, latest %.1fms)! Scene: %s, Menu: %s, CPU work: %.1fms, Render: %.1fms, QueueWait: %.1fms, Compile: %.1fms, PipelineCreate: %.1fms",
                         sConsecutiveSlowFrames, frameIntervalMs, sCurrentScene, sMenuOpen ? "OPEN" : "CLOSED",
                         sWork.avg(), sRenderCpu.avg(), sQueueWait.avg(), sShaderCompile.sum, sPipelineCreate.sum);
            }
        } else {
            if (sConsecutiveSlowFrames >= 30) {
                port_log("perf DIAGNOSTIC RECOVERY: frame rate restored to 60 FPS after %u slow frames (Scene: %s)",
                         sConsecutiveSlowFrames, sCurrentScene);
            }
            sConsecutiveSlowFrames = 0;
            sSlowdownReported = false;
        }
    }
    sFrameStart = now;
    sRecordThisFrame = 0;
}

void port_perf_frame_work_done(void) {
    std::lock_guard<std::mutex> lock(sLock);
    int64_t now = port_host_time_ns();
    if (sFrameStart) {
        sWork.add((now - sFrameStart) / 1e6);
        sRecord.add(sRecordThisFrame / 1e6);
        sFrames++;
    }
    sLastFrameEnd = now;
    if (now - sWindowStart >= 10000000000ll) {
        report(now);
    }
}

void port_perf_record(int64_t ns) {
    std::lock_guard<std::mutex> lock(sLock);
    sRecordThisFrame += ns;
}

void port_perf_prepare(int64_t ns) {
    std::lock_guard<std::mutex> lock(sLock);
    sPrepare.add(ns / 1e6);
}

void port_perf_gpu_wait(int64_t ns) {
    std::lock_guard<std::mutex> lock(sLock);
    sGpuWait.add(ns / 1e6);
}

void port_perf_render(int64_t cpuNs, int64_t uploadNs) {
    std::lock_guard<std::mutex> lock(sLock);
    sRenderCpu.add(cpuNs / 1e6);
    sUpload.add(uploadNs / 1e6);
    sRenderFrames++;
}

void port_perf_update(int64_t ns) {
    std::lock_guard<std::mutex> lock(sLock);
    sUpdate.add(ns / 1e6);
}

void port_perf_present(int64_t ns) {
    std::lock_guard<std::mutex> lock(sLock);
    sPresent.add(ns / 1e6);
}

void port_perf_pace(int64_t ns) {
    std::lock_guard<std::mutex> lock(sLock);
    sPace.add(ns / 1e6);
}

void port_perf_shader_compile(int64_t ns) {
    std::lock_guard<std::mutex> lock(sLock);
    sShaderCompile.add(ns / 1e6);
}

void port_perf_pipeline_create(int64_t ns) {
    std::lock_guard<std::mutex> lock(sLock);
    sPipelineCreate.add(ns / 1e6);
}

void port_perf_queue_wait(int64_t ns) {
    std::lock_guard<std::mutex> lock(sLock);
    sQueueWait.add(ns / 1e6);
}

void port_perf_scene_transition(const char* scene) {
    std::lock_guard<std::mutex> lock(sLock);
    if (scene) {
        strncpy(sCurrentScene, scene, sizeof(sCurrentScene) - 1);
        sCurrentScene[sizeof(sCurrentScene) - 1] = '\0';
    }
}

void port_perf_menu_event(int opened) {
    std::lock_guard<std::mutex> lock(sLock);
    sMenuOpen = (opened != 0);
}

}  // extern "C"
