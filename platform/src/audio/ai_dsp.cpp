#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "port/heap_routing.h"
#include "port/port.h"
#include "revolution/ai.h"
#include "revolution/os.h"

// ---------------------------------------------------------------------------
// Audio interface DMA, paced by the host audio device (see aiClock).
// ---------------------------------------------------------------------------
static AIDCallback sDmaCallback;
static std::atomic<u32> sDmaAddr{0};
static std::atomic<u32> sDmaLen{0};
static std::atomic<u32> sDmaLatched{0};
static std::atomic<bool> sDmaRunning{false};
static std::atomic<u32> sSampleRate{32000};
static bool sAiThreadStarted;

extern "C" void __attribute__((weak)) port_audio_submit(const s16* interleavedLE, u32 frames, u32 rate) {
    (void)interleavedLE;
    (void)frames;
    (void)rate;
}
extern "C" int64_t __attribute__((weak)) port_audio_queued_ns(void) { return -1; }
extern "C" int __attribute__((weak)) port_audio_underruns(void) { return 0; }

static inline const u8* memPtr(u32 addr) {
    if (addr >= 0x80000000u) {
        return (const u8*)(uintptr_t)addr;
    }
    return (const u8*)port_phys_to_host(addr);
}

static void aiIrq() {
    AIDCallback cb = sDmaCallback;
    if (cb) {
        cb();
    }
}

static void aiClock() {
#if TARGET_MOBILE
    const int64_t kTarget = 48000000;  // 48 ms queue headroom on mobile
    const int64_t kLow = 16000000;
#else
    const int64_t kTarget = 32000000;
    const int64_t kLow = 8000000;
#endif
    int64_t next = port_host_time_ns();
    u32 answered = sDmaLatched.load(std::memory_order_relaxed);
    u32 blocks = 0, late = 0;
    int64_t reportAt = next + 10000000000ll, minQueued = INT64_MAX, maxQueued = -1;
    static const bool logAlways = getenv("PETARI_AUDIOLOG") != nullptr;

    for (;;) {
        if (!sDmaRunning.load(std::memory_order_relaxed) || port_is_paused()) {
            port_host_sleep_ns(2000000);
            next = port_host_time_ns();
            continue;
        }
        u32 rate = sSampleRate.load(std::memory_order_relaxed);
        u32 len = sDmaLen.load(std::memory_order_relaxed);
        u32 frames = len / 4;
        int64_t dur = frames ? (int64_t)frames * 1000000000ll / (rate ? rate : 32000) : 5000000;
        int64_t now = port_host_time_ns();
        int64_t queued = port_audio_queued_ns();
        if (queued >= 0) {
            if (queued > kTarget) {
                int64_t wait = queued - kTarget;
                port_host_sleep_ns(wait < 4000000 ? wait : 4000000);
                continue;
            }
        } else if (now < next) {
            port_host_sleep_ns(next - now);
            continue;
        }

        if (sDmaLatched.load(std::memory_order_relaxed) == answered) {
            bool slack = queued >= 0 ? queued > kLow : now < next + dur / 2;
            if (slack) {
                port_host_sleep_ns(500000);
                continue;
            }
            late++;
        }
        answered = sDmaLatched.load(std::memory_order_relaxed);
        u32 addr = sDmaAddr.load(std::memory_order_relaxed);
        frames = sDmaLen.load(std::memory_order_relaxed) / 4;
        if (addr && frames) {
            port_audio_submit((const s16*)memPtr(addr), frames, rate);
        }
        port_irq_raise(PORT_IRQ_AI);
        blocks++;
        if (queued < minQueued) minQueued = queued;
        if (queued > maxQueued) maxQueued = queued;
        next += dur;
        if (now - next > 100000000) {
            next = now;
        }
        if (now >= reportAt) {
            static int lastUnderruns = 0;
            int underruns = port_audio_underruns();
            if (late || underruns != lastUnderruns || logAlways) {
                port_log("audio: %u blocks, %u replayed (game late), %d device underruns, queued %.1f-%.1f ms",
                         blocks, late, underruns - lastUnderruns,
                         minQueued / 1e6, maxQueued / 1e6);
            }
            lastUnderruns = underruns;
            late = blocks = 0;
            minQueued = INT64_MAX;
            maxQueued = -1;
            reportAt = now + 10000000000ll;
        }
    }
}

extern "C" {

void AIInit(u8* stack) {
    (void)stack;
    port_irq_set_handler(PORT_IRQ_AI, aiIrq);
    if (!sAiThreadStarted) {
        sAiThreadStarted = true;
        PortHostAllocScope scope;
        std::thread(aiClock).detach();
    }
}

void AISetDSPEntrypoint(void*) {}
void AISetDSPLength(u32) {}

void AIStartDMA(void) {
    sDmaRunning.store(true, std::memory_order_release);
    if (!sAiThreadStarted) {
        sAiThreadStarted = true;
        port_irq_set_handler(PORT_IRQ_AI, aiIrq);
        PortHostAllocScope scope;
        std::thread(aiClock).detach();
    }
}

void AISetDSPSampleRate(u32 rate) { sSampleRate.store(rate ? 48000 : 32000, std::memory_order_relaxed); }
u32 AIGetDSPSampleRate(void) { return sSampleRate.load(std::memory_order_relaxed) == 48000 ? 1 : 0; }

void AIStopDMA(void) { sDmaRunning.store(false, std::memory_order_release); }

BOOL AIGetDMAEnableFlag(void) { return sDmaRunning.load(std::memory_order_relaxed) ? TRUE : FALSE; }

void AIInitDMA(u32 addr, u32 len) {
    sDmaAddr.store(addr, std::memory_order_relaxed);
    sDmaLen.store(len, std::memory_order_relaxed);
    sDmaLatched.fetch_add(1, std::memory_order_relaxed);
}

BOOL AICheckInit(void) { return TRUE; }

AIDCallback AIRegisterDMACallback(AIDCallback callback) {
    AIDCallback old = sDmaCallback;
    sDmaCallback = callback;
    return old;
}

u32 AIGetDMABytesLeft(void) {
    // JAudio polls this when waiting for a stream to stop; we say it's done.
    return 0;
}

u32 AIGetDMAStartAddr(void) { return sDmaAddr.load(std::memory_order_relaxed); }
u32 AIGetDMALength(void) { return sDmaLen.load(std::memory_order_relaxed); }

void AISetStreamSampleRate(u32 rate) {
    // 0 = 48 kHz, 1 = 32 kHz
    sSampleRate.store(rate == 0 ? 48000 : 32000, std::memory_order_relaxed);
}

u32 AIGetStreamSampleRate(void) { return sSampleRate.load(std::memory_order_relaxed) == 48000 ? 0 : 1; }

void AISetStreamVolLeft(u8) {}
u8 AIGetStreamVolLeft(void) { return 255; }
void AISetStreamVolRight(u8) {}
u8 AIGetStreamVolRight(void) { return 255; }
void AIResetStreamSampleCount(void) {}
u32 AIGetStreamSampleCount(void) { return 0; }
void AISetStreamTrigger(u32) {}
void AISetStreamPlayState(u32) {}
u32 AIGetStreamPlayState(void) { return 0; }

}  // extern "C"

extern "C" void port_ai_set_stream_sample_rate(u32 rate) {
    sSampleRate.store(rate == 48000 ? 48000 : 32000, std::memory_order_relaxed);
}
