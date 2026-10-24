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
#include "vi_timing.h"

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
static std::atomic<float> sSpeedMultiplier{1.0f};

static inline int64_t getSimulationPeriodNs() {
    return port_vi_timing::simulationPeriodNs(sSpeedMultiplier.load());
}

// Raises the retrace interrupt: enforces authentic Wii ~59.94 Hz simulation cadence
// scaled ONLY by the user's game speed multiplier, completely independent of display refresh rate.
// Allows phase-locking to display flips (60/120Hz) with ~12% jitter tolerance, but strictly prevents
// running simulation faster than the Wii's fixed timestep (fixing Issue #112 fast forward at 90/120Hz).
static void raiseRetrace() {
    int64_t now = port_host_time_ns();
    int64_t last = sRaisedAt.load();
    if (!port_vi_timing::retraceAllowed(now, last, getSimulationPeriodNs())) {
        return;
    }
    if (!sRaisedAt.compare_exchange_strong(last, now)) {
        return;
    }
    port_irq_raise(PORT_IRQ_VI);
}

// Implemented by the renderer: show the frame the game copied to `xfb`.
extern "C" void port_gx_present_xfb(void* xfb, int black);

static void viIrq() {
    sRetraceCount = sRetraceCount + 1;
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

static void viClock() {
    int64_t period = getSimulationPeriodNs();
    int64_t next = port_host_time_ns() + period;
    for (;;) {
        period = getSimulationPeriodNs();
        int64_t now = port_host_time_ns();
        if (next > now) {
            port_host_sleep_ns(next - now);
        }
        now = port_host_time_ns();
        next = port_vi_timing::advanceDeadline(next, now, period);
        if (sPaused.load()) {
            continue;  // no retrace: the game stays in VIWaitForRetrace
        }
        // A display-timed retrace already serving this cadence window makes
        // this tick redundant; a slower presentation rate leaves gaps that
        // the fallback clock must fill to keep simulation at 60 Hz.
        if (!port_vi_timing::fallbackTickDue(now, sRaisedAt.load(), period)) {
            continue;
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
    if (hz < 60) hz = 60;
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
