// Audio interface (AI) and the DSP mailbox, emulated at the level JAudio2's
// own driver (dsptask.cpp / osdsp_task.cpp) talks to.
//
// The DSP side implements the JAudio microcode's mailbox protocol:
//   boot            -> mails 0xDCD10000, <any>      (DspHandShake)
//   command packet  -> [count][word0..]             (DSPSendCommands2)
//   0x82 syncFrame  -> N sub-frames to render, each when the game releases the voices
//   release halt    -> with the last voices released: one sub-frame mixed, then 0xDCD10004,0xF355FF00
//   0x81/0x8E etc.  -> 0xDCD10004, 0xF355<word0 >> 16> (DspFinishWork)
// Mixing is delegated to port_dsp_mix_frame (silence until the mixer lands).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "port/heap_routing.h"
#include "port/port.h"
#include "revolution/ai.h"
#include "revolution/dsp.h"
#include "revolution/os.h"

// ---------------------------------------------------------------------------
// DSP mailbox
// ---------------------------------------------------------------------------
extern "C" {
DSPTaskInfo* __DSP_first_task;
DSPTaskInfo* __DSP_curr_task;
DSPTaskInfo* __DSP_last_task;
DSPTaskInfo* __DSP_tmp_task;
void __DSPHandler(__OSInterrupt interrupt, OSContext* context);
}

static std::mutex sMailMutex;
static std::deque<u32> sMailFromDsp;

static std::vector<u32> sPacket;
static u32 sPacketExpected = 0;
static bool sWaitingCount = true;
static u32 sVaramBase = 0;
static u16 sMixerLevel = 0x4000;

extern "C" u32 port_dsp_varam_base(void) { return sVaramBase; }

static void queueMail(u32 a, u32 b) {
    std::lock_guard<std::mutex> lk(sMailMutex);
    sMailFromDsp.push_back(a);
    sMailFromDsp.push_back(b);
}

// Mixer setup (command 0x81); implemented in dsp_mixer.cpp.
extern "C" void __attribute__((weak)) port_dsp_setup(u32 voiceCount, u32 voices, u32 table, u32 afcTable, u32 fxBlocks) {
    (void)voiceCount, (void)voices, (void)table, (void)afcTable, (void)fxBlocks;
}

// Software mixer entry point (implemented in the audio mixer).
extern "C" void __attribute__((weak)) port_dsp_mix_frame(u32 subFrames, s16* outL, s16* outR, u16 mixerLevel) {
    (void)mixerLevel;
    memset(outL, 0, subFrames * 80 * sizeof(s16));
    memset(outR, 0, subFrames * 80 * sizeof(s16));
}

// The sync frame being rendered.  As on the console it goes one sub-frame at
// a time: the game updates the voices (JASDSPChannel::updateAll), releasing
// them to the DSP sixteen at a time; with the last sixteen released the DSP
// renders the sub-frame's 80 samples and reports with a mail, on which the
// game updates the voices for the next one.  The game so follows each voice
// 80 samples at a time, and JASAramStream counts on it: it sets up a stream's
// loop (or its end) once the voice is within 400 samples of it, and takes a
// voice found beyond it for a fatal error that silences every stream until
// the game is restarted.  Rendered a whole frame at once, a voice moved 560
// samples between two looks and jumped that window on about one pass in
// four: the music of the file select screen stopped for good after 55 s.
static struct {
    u32 subFrames = 0;
    u32 done = 0;
    s16* outL = nullptr;
    s16* outR = nullptr;
} sFrame;

static void executePacket() {
    const std::vector<u32>& p = sPacket;
    u32 w0 = p.empty() ? 0 : p[0];
    u32 cmd = w0 >> 24;
    switch (cmd) {
    case 0x82:  // sync frame: `subFrames` sub-frames of 80 samples to render
        sFrame.subFrames = (w0 >> 16) & 0xFF;
        sFrame.done = 0;
        sMixerLevel = (u16)(w0 & 0xFFFF);
        sFrame.outL = (s16*)(uintptr_t)(p.size() > 1 ? p[1] : 0);
        sFrame.outR = (s16*)(uintptr_t)(p.size() > 2 ? p[2] : 0);
        break;
    case 0x00:  // release halt: voices 0 .. 16 * (group + 1) - 1 are ready for the sub-frame
        if (((w0 >> 16) & 0xFF) == 3 && sFrame.done < sFrame.subFrames) {
            if (sFrame.outL && sFrame.outR) {
                port_dsp_mix_frame(1, sFrame.outL + sFrame.done * 80, sFrame.outR + sFrame.done * 80, sMixerLevel);
            }
            sFrame.done++;
            queueMail(0xDCD10004, 0xF355FF00);
        }
        break;
    case 0x8E:  // set VARAM base (alt-ARAM start in MEM2)
        sVaramBase = p.size() > 1 ? p[1] : 0;
        queueMail(0xDCD10004, 0xF3550000 | (w0 >> 16));
        break;
    case 0x81:  // table setup (channel buffer, filters, FX buffer)
        if (p.size() >= 5) {
            port_dsp_setup(w0 & 0xFFFF, p[1], p[2], p[3], p[4]);
        }
        queueMail(0xDCD10004, 0xF3550000 | (w0 >> 16));
        break;
    default:
        // Other control words need no reply.
        break;
    }
    port_irq_raise(PORT_IRQ_DSP);
}

static void dspIrq() {
    // Each __DSPHandler call consumes one event (a DCD1 mail and, for requests,
    // the payload mail read by the task's callback).
    for (int guard = 0; guard < 256; guard++) {
        {
            std::lock_guard<std::mutex> lk(sMailMutex);
            if (sMailFromDsp.empty()) {
                break;
            }
        }
        __DSPHandler(__OS_INTERRUPT_DSP_DSP, OSGetCurrentContext());
    }
}

extern "C" {

void DSPInit(void) {
    port_irq_set_handler(PORT_IRQ_DSP, dspIrq);
}

u32 DSPCheckMailToDSP(void) { return 0; }  // the HLE consumes mail instantly

u32 DSPCheckMailFromDSP(void) {
    std::lock_guard<std::mutex> lk(sMailMutex);
    return sMailFromDsp.empty() ? 0 : 0x80000000u;
}

u32 DSPReadMailFromDSP(void) {
    std::lock_guard<std::mutex> lk(sMailMutex);
    if (sMailFromDsp.empty()) {
        return 0;
    }
    u32 m = sMailFromDsp.front();
    sMailFromDsp.pop_front();
    return m;
}

void DSPSendMailToDSP(u32 mail) {
    if (sWaitingCount) {
        sPacket.clear();
        sPacketExpected = mail == 0 ? 1 : mail;
        sWaitingCount = false;
        return;
    }
    sPacket.push_back(mail);
    if (sPacket.size() >= sPacketExpected) {
        sWaitingCount = true;
        executePacket();
    }
}

void DSPAssertInt(void) {}

void __DSP_boot_task(DSPTaskInfo* task) {
    __DSP_curr_task = task;
    // Microcode is "running": report init, then the second handshake mail.
    queueMail(0xDCD10000, 0x8071FEED);
    port_irq_raise(PORT_IRQ_DSP);
}

void __DSP_exec_task(DSPTaskInfo*, DSPTaskInfo*) {}
void __DSP_remove_task(DSPTaskInfo*) {}

}  // extern "C"

// ---------------------------------------------------------------------------
// Audio interface DMA, paced by the host audio device (see aiClock).
// ---------------------------------------------------------------------------
static AIDCallback sDmaCallback;
static std::atomic<u32> sDmaAddr{0};
static std::atomic<u32> sDmaLen{0};
static std::atomic<u32> sDmaLatched{0};  // AIInitDMA calls so far
static std::atomic<bool> sDmaRunning{false};
static std::atomic<u32> sSampleRate{32000};
static bool sAiThreadStarted;

// Host audio sink (implemented by the platform audio output): plays a block,
// reports how much audio the device has queued (< 0 without a device) and
// how often it ran dry.
extern "C" void __attribute__((weak)) port_audio_submit(const s16* interleavedLE, u32 frames, u32 rate) {
    (void)interleavedLE;
    (void)frames;
    (void)rate;
}
extern "C" int64_t __attribute__((weak)) port_audio_queued_ns(void) { return -1; }
extern "C" int __attribute__((weak)) port_audio_underruns(void) { return 0; }

static void aiIrq() {
    if (sDmaCallback) {
        sDmaCallback();
    }
}

// The AI hardware: plays the block the game latched with AIInitDMA and
// interrupts as it starts, so the game can latch the next one (JAudio mixes
// a block per interrupt).  A new block starts whenever the device has less
// than kTarget queued, so the device's clock paces the game's audio; without
// a device, a timer does.
static void aiClock() {
    // The game answers an interrupt from its audio thread, which only gets
    // the CPU at the running thread's next safe point, sometimes 10-20 ms
    // later.  The queued audio covers that.
#if TARGET_MOBILE
    const int64_t kTarget = 48000000;  // 48 ms queue headroom on mobile
    const int64_t kLow = 16000000;
#else
    const int64_t kTarget = 32000000;
    const int64_t kLow = 8000000;
#endif
    int64_t next = port_host_time_ns();  // start of the next block (timer pacing)
    u32 answered = sDmaLatched.load();
    u32 blocks = 0, late = 0;
    int64_t reportAt = next + 10000000000ll, minQueued = INT64_MAX, maxQueued = -1;
    static const bool logAlways = getenv("PETARI_AUDIOLOG") != nullptr;
    for (;;) {
        if (!sDmaRunning.load() || port_is_paused()) {
            port_host_sleep_ns(2000000);
            next = port_host_time_ns();
            continue;
        }
        u32 rate = sSampleRate.load();
        u32 frames = sDmaLen.load() / 4;
        int64_t dur = frames ? (int64_t)frames * 1000000000ll / (rate ? rate : 32000) : 5000000;
        int64_t now = port_host_time_ns();
        int64_t queued = port_audio_queued_ns();  // < 0: no device
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
        if (sDmaLatched.load() == answered) {
            bool slack = queued >= 0 ? queued > kLow : now < next + dur / 2;
            if (slack) {
                port_host_sleep_ns(500000);
                continue;
            }
            late++;
        }
        answered = sDmaLatched.load();
        u32 addr = sDmaAddr.load();
        frames = sDmaLen.load() / 4;
        if (addr && frames) {
            port_audio_submit((const s16*)(uintptr_t)addr, frames, rate);
        }
        port_irq_raise(PORT_IRQ_AI);
        blocks++;
        if (queued < minQueued) minQueued = queued;
        if (queued > maxQueued) maxQueued = queued;
        next += dur;
        if (now - next > 100000000) {
            next = now;  // resync after a long stall instead of bursting
        }
        if (now >= reportAt) {
            // PETARI_AUDIOLOG=1 reports every 10 s; otherwise only trouble.
            static int lastUnderruns = 0;
            int underruns = port_audio_underruns();
            if (late || underruns != lastUnderruns || logAlways) {
                port_log("audio: %u blocks, %u replayed (game late), %d device underruns, queued %.1f-%.1f ms", blocks, late, underruns - lastUnderruns,
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

AIDCallback AIRegisterDMACallback(AIDCallback cb) {
    AIDCallback old = sDmaCallback;
    sDmaCallback = cb;
    return old;
}

void AIInitDMA(u32 addr, u32 length) {
    sDmaAddr.store(addr);
    sDmaLen.store(length);
    sDmaLatched++;
}

void AIStartDMA(void) { sDmaRunning.store(true); }
void AIStopDMA(void) { sDmaRunning.store(false); }

void AISetDSPSampleRate(u32 rate) { sSampleRate.store(rate ? 48000 : 32000); }
u32 AIGetDSPSampleRate(void) { return sSampleRate.load() == 48000 ? 1 : 0; }

}  // extern "C"
