#include <stdint.h>
#include <string.h>

#include <deque>
#include <mutex>

#include "port/port.h"
#include "revolution/dsp.h"
#include "revolution/os.h"

extern "C" {
DSPTaskInfo* __DSP_first_task;
DSPTaskInfo* __DSP_curr_task;
DSPTaskInfo* __DSP_last_task;
DSPTaskInfo* __DSP_tmp_task;
void __DSPHandler(__OSInterrupt interrupt, OSContext* context);
}

static std::mutex sMailMutex;
static std::deque<u32> sMailFromDsp;

static std::mutex sDspCmdMutex;
static u32 sPacket[64];
static u32 sPacketSize = 0;
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

extern "C" void __attribute__((weak)) port_dsp_setup(u32 voiceCount, u32 voices, u32 table, u32 afcTable, u32 fxBlocks) {
    (void)voiceCount, (void)voices, (void)table, (void)afcTable, (void)fxBlocks;
}

extern "C" void __attribute__((weak)) port_dsp_mix_frame(u32 subFrames, s16* outL, s16* outR, u16 mixerLevel) {
    (void)mixerLevel;
    memset(outL, 0, subFrames * 80 * sizeof(s16));
    memset(outR, 0, subFrames * 80 * sizeof(s16));
}

static struct {
    u32 subFrames = 0;
    u32 done = 0;
    s16* outL = nullptr;
    s16* outR = nullptr;
} sFrame;

static void executePacket() {
    u32 w0 = sPacketSize > 0 ? sPacket[0] : 0;
    u32 cmd = w0 >> 24;
    switch (cmd) {
    case 0x82:
        sFrame.subFrames = (w0 >> 16) & 0xFF;
        sFrame.done = 0;
        sMixerLevel = (u16)(w0 & 0xFFFF);
        sFrame.outL = (s16*)(uintptr_t)(sPacketSize > 1 ? sPacket[1] : 0);
        sFrame.outR = (s16*)(uintptr_t)(sPacketSize > 2 ? sPacket[2] : 0);
        break;
    case 0x00:
        if (((w0 >> 16) & 0xFF) == 3 && sFrame.done < sFrame.subFrames) {
            if (sFrame.outL && sFrame.outR) {
                port_dsp_mix_frame(1, sFrame.outL + sFrame.done * 80, sFrame.outR + sFrame.done * 80, sMixerLevel);
            }
            sFrame.done++;
            queueMail(0xDCD10004, 0xF355FF00);
        }
        break;
    case 0x8E:
        sVaramBase = sPacketSize > 1 ? sPacket[1] : 0;
        queueMail(0xDCD10004, 0xF3550000 | (w0 >> 16));
        break;
    case 0x81:
        if (sPacketSize >= 5) {
            port_dsp_setup(w0 & 0xFFFF, sPacket[1], sPacket[2], sPacket[3], sPacket[4]);
        }
        queueMail(0xDCD10004, 0xF3550000 | (w0 >> 16));
        break;
    default:
        break;
    }
    port_irq_raise(PORT_IRQ_DSP);
}

static void dspIrq() {
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

u32 DSPCheckMailToDSP(void) { return 0; }

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
    if ((mail & 0xFFFF0000) == 0xCDD10000) {
        return;
    }
    std::lock_guard<std::mutex> lk(sDspCmdMutex);
    if (sWaitingCount) {
        sPacketSize = 0;
        sPacketExpected = (mail == 0) ? 1 : mail;
        sWaitingCount = false;
        return;
    }
    if (sPacketSize < 64) {
        sPacket[sPacketSize] = mail;
    }
    sPacketSize++;
    if (sPacketSize >= sPacketExpected) {
        sWaitingCount = true;
        executePacket();
    }
}

void DSPAssertInt(void) {}

void __DSP_boot_task(DSPTaskInfo* task) {
    __DSP_curr_task = task;
    queueMail(0xDCD10000, 0x8071FEED);
    port_irq_raise(PORT_IRQ_DSP);
}

void __DSP_exec_task(DSPTaskInfo*, DSPTaskInfo*) {}
void __DSP_remove_task(DSPTaskInfo*) {}

}  // extern "C"
