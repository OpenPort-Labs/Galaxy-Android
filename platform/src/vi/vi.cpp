// Video interface: the retrace clock driving the game's frame pacing.
// The frame shown to the player is chosen by the renderer from the XFB the
// game flips to here.  In the headset the display times the retraces
// (port_vi_retrace, every second refresh at 120 Hz); otherwise, and as a
// fallback, a free-running 59.94 Hz clock does.
#include <atomic>
#include <thread>

#include "port/heap_routing.h"
#include "port/port.h"
#include "revolution/os/OSInterrupt.h"
#include "revolution/os/OSThread.h"
#include "revolution/vi/vi3in1types.h"
#include "revolution/vi/vifuncs.h"
#include "revolution/vi/vitypes.h"

static VIRetraceCallback sPreCb;
static VIRetraceCallback sPostCb;
static volatile u32 sRetraceCount;
static OSThreadQueue sRetraceQueue;
static void* sNextFb;
static void* sCurrentFb;
static bool sFlushPending;
static bool sBlack = true;
static bool sPendingBlack = true;
static const GXRenderModeObj* sRenderMode;
static bool sInited;
static std::atomic<int> sPaused{0};
static std::atomic<int64_t> sExternalAt{0};  // host time of the last display-timed retrace
static std::atomic<int64_t> sRaisedAt{0};    // host time of the last retrace raised
static std::atomic<int> sDisplayRefreshRate{60};

// Raises the retrace interrupt, unless one was raised less than 8 ms ago: as
// on the console, retraces are a field apart. When the display takes the
// retraces back from the fallback clock, the two could otherwise land a
// millisecond apart, and the game drew two frames within one retrace.
static void raiseRetrace() {
    int64_t now = port_host_time_ns();
    int64_t last = sRaisedAt.load();
    if (now - last < 8000000 || !sRaisedAt.compare_exchange_strong(last, now)) {
        return;
    }
    port_irq_raise(PORT_IRQ_VI);
}

// Implemented by the renderer: show the frame the game copied to `xfb`.
extern "C" void port_gx_present_xfb(void* xfb, int black);

static void viIrq() {
    sRetraceCount++;
    if (sPreCb) {
        sPreCb(sRetraceCount);
    }
    if (sFlushPending) {
        sCurrentFb = sNextFb;
        sBlack = sPendingBlack;
        sFlushPending = false;
        port_gx_present_xfb(sCurrentFb, sBlack ? 1 : 0);
    }
    if (sPostCb) {
        sPostCb(sRetraceCount);
    }
    OSWakeupThread(&sRetraceQueue);
}

static std::atomic<float> sSpeedMultiplier{1.0f};

static void viClock() {
    // NTSC / EURGB60 field rate.
    const int64_t basePeriod = 16683350;  // ns (1001/60000 s)
    float speed = sSpeedMultiplier.load();
    int64_t period = (int64_t)((double)basePeriod / (speed > 0.1f ? speed : 1.0f));
    int64_t next = port_host_time_ns() + period;
    for (;;) {
        speed = sSpeedMultiplier.load();
        period = (int64_t)((double)basePeriod / (speed > 0.1f ? speed : 1.0f));
        int64_t now = port_host_time_ns();
        if (next > now) {
            port_host_sleep_ns(next - now);
        }
        next += period;
        now = port_host_time_ns();
        if (now > next + period * 4) {
            next = now + period;  // resync after a long stall instead of bursting
        }
        if (sPaused.load()) {
            continue;  // no retrace: the game stays in VIWaitForRetrace
        }
        if (now - sExternalAt.load() < period * 4) {
            continue;  // the display times the retraces
        }
        raiseRetrace();
    }
}

extern "C" {

void port_vi_retrace(void) {
    sExternalAt.store(port_host_time_ns());
    if (!sPaused.load()) {
        raiseRetrace();
    }
}

void port_set_display_refresh_rate(int hz) {
    if (hz < 30) hz = 30;
    if (hz > 240) hz = 240;
    sDisplayRefreshRate.store(hz);
}

int port_get_display_refresh_rate(void) {
    return sDisplayRefreshRate.load();
}

void port_set_paused(int paused) {
    if (sPaused.exchange(paused ? 1 : 0) != (paused ? 1 : 0)) {
        port_log("game %s", paused ? "paused" : "resumed");
    }
}

int port_is_paused(void) { return sPaused.load(); }

void port_set_game_speed(float speed) {
    if (speed < 0.25f) speed = 0.25f;
    if (speed > 5.0f) speed = 5.0f;
    sSpeedMultiplier.store(speed);
}

float port_get_game_speed(void) {
    return sSpeedMultiplier.load();
}

void VIInit(void) {
    if (sInited) {
        return;
    }
    sInited = true;
    OSInitThreadQueue(&sRetraceQueue);
    port_irq_set_handler(PORT_IRQ_VI, viIrq);
    PortHostAllocScope scope;
    std::thread(viClock).detach();
}

void VIConfigure(const GXRenderModeObj* rm) { sRenderMode = rm; }
void VIConfigurePan(u16, u16, u16, u16) {}

void VIFlush(void) {
    BOOL en = OSDisableInterrupts();
    sFlushPending = true;
    OSRestoreInterrupts(en);
}

void VISetNextFrameBuffer(void* fb) {
    BOOL en = OSDisableInterrupts();
    sNextFb = fb;
    OSRestoreInterrupts(en);
}

void* VIGetNextFrameBuffer(void) { return sNextFb; }
void* VIGetCurrentFrameBuffer(void) { return sCurrentFb; }

void VIWaitForRetrace(void) {
    BOOL en = OSDisableInterrupts();
    u32 count = sRetraceCount;
    do {
        OSSleepThread(&sRetraceQueue);
    } while (count == sRetraceCount);
    OSRestoreInterrupts(en);
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb) {
    BOOL en = OSDisableInterrupts();
    VIRetraceCallback old = sPreCb;
    sPreCb = cb;
    OSRestoreInterrupts(en);
    return old;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb) {
    BOOL en = OSDisableInterrupts();
    VIRetraceCallback old = sPostCb;
    sPostCb = cb;
    OSRestoreInterrupts(en);
    return old;
}

void VISetBlack(BOOL black) {
    BOOL en = OSDisableInterrupts();
    sPendingBlack = black ? true : false;
    OSRestoreInterrupts(en);
}

u32 VIGetRetraceCount(void) { return sRetraceCount; }
u32 VIGetCurrentLine(void) { return 0; }
u32 VIGetTvFormat(void) { return VI_EURGB60; }
u32 VIGetDTVStatus(void) { return 1; }  // component/digital cable: allow progressive
BOOL VIEnableDimming(BOOL enable) {
    (void)enable;
    return FALSE;
}
// Retraces left before the console dims the picture against burn-in (five
// minutes without input).  Never 0 here: at 0 the game takes the screen saver
// to be on and keeps the music at a third of its volume (AudSystem::
// screenSaverProcess).
u32 VIGetDimmingCount(void) { return 18000; }
void VISetTrapFilter(VIBool) {}

}  // extern "C"
