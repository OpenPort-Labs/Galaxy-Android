// Frame-time statistics for the game thread and the renderer, logged every
// 10 s: always with PETARI_PERFLOG=1, otherwise only while the game runs
// below 58 fps (so the headset's log shows slowdowns).  On Android an app
// process has no environment to set, so `adb shell setprop debug.petari.perflog 1`
// turns the same logging on for field diagnostics.
#include <stdlib.h>

#include <atomic>
#include <mutex>

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
unsigned sFrames = 0, sRenderFrames = 0;
int64_t sFrameStart = 0, sRecordThisFrame = 0, sWindowStart = 0;
// Read on each report (once per 10 s): the VR app sets debug variables after the
// library loads, and the Android property can be changed between launches.
bool alwaysLog() {
    if (getenv("PETARI_PERFLOG") != nullptr) return true;
#if defined(__ANDROID__)
    char value[PROP_VALUE_MAX] = {0};
    if (__system_property_get("debug.petari.perflog", value) > 0 && value[0] == '1') return true;
    if (__system_property_get("persist.petari.perflog", value) > 0 && value[0] == '1') return true;
#endif
    return false;
}

void report(int64_t now) {
    double secs = (now - sWindowStart) / 1e9;
    double fps = sFrames / secs;
    if (alwaysLog() || fps < 58.0) {
        port_log("perf: game %.1f fps, work %.1f ms avg %.1f max (GX recording %.1f avg); renderer %.1f fps, cpu %.1f ms avg %.1f max, "
                 "upload %.1f avg, prepare %.1f avg %.1f max",
                 fps, sWork.avg(), sWork.max, sRecord.avg(), sRenderFrames / secs, sRenderCpu.avg(), sRenderCpu.max, sUpload.avg(), sPrepare.avg(),
                 sPrepare.max);
        if (sGpuWait.count) {
            port_log("perf: GPU finished %.1f ms avg %.1f max after the render thread's submission", sGpuWait.avg(), sGpuWait.max);
        }
        if (sPresent.count || sUpdate.count || sPace.count) {
            port_log("perf: render thread - update %.1f avg %.1f max, present %.1f avg %.1f max, pacer slept %.1f avg %.1f max",
                     sUpdate.avg(), sUpdate.max, sPresent.avg(), sPresent.max, sPace.avg(), sPace.max);
        }
    }
    sWork = sRecord = sPrepare = sRenderCpu = sUpload = sGpuWait = Stat();
    sUpdate = sPresent = sPace = Stat();
    sFrames = sRenderFrames = 0;
    sWindowStart = now;
}

}  // namespace

extern "C" {

void port_perf_frame_begin(void) {
    std::lock_guard<std::mutex> lock(sLock);
    int64_t now = port_host_time_ns();
    if (!sWindowStart) sWindowStart = now;
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

}  // extern "C"
