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

// Blocks of 160 frames (5 ms at 32 kHz, 10 sub-frames).
static const u32 kAiBlockFrames = 160;

static void aiClockThread() {
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (;;) {
        u32 rate = sSampleRate.load(std::memory_order_relaxed);
        u64 periodNs = (u64)kAiBlockFrames * 1000000000ull / rate;
        next.tv_nsec += (long)periodNs;
        if (next.tv_nsec >= 1000000000l) {
            next.tv_sec += next.tv_nsec / 1000000000l;
            next.tv_nsec %= 1000000000l;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);

        if (!sDmaRunning.load(std::memory_order_relaxed)) {
            continue;
        }
        u32 addr = sDmaAddr.load(std::memory_order_relaxed);
        u32 len = sDmaLen.load(std::memory_order_relaxed);
        if (!addr || !len) {
            continue;
        }

        const s16* src = (const s16*)memPtr(addr);
        port_audio_submit(src, kAiBlockFrames, rate);

        port_irq_raise(PORT_IRQ_AI);
    }
}

extern "C" {

void AIInit(u8* stack) {
    (void)stack;
    port_irq_set_handler(PORT_IRQ_AI, aiIrq);
    if (!sAiThreadStarted) {
        sAiThreadStarted = true;
        PortHostAllocScope scope;
        std::thread(aiClockThread).detach();
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
        std::thread(aiClockThread).detach();
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
