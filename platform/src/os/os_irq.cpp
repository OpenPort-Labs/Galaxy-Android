#include "os_internal.h"

bool sIntEnabled = false;
bool sInIrq = false;
std::atomic<u32> sPendingIrq{0};
std::mutex sIdleMutex;
std::condition_variable sIdleCv;
static PortIrqHandler sIrqHandlers[PORT_IRQ_COUNT];
static __OSInterruptHandler sSdkHandlers[32];

extern "C" void port_irq_set_handler(int irq, PortIrqHandler handler) {
    sIrqHandlers[irq] = handler;
}

extern "C" void port_irq_raise(int irq) {
    sPendingIrq.fetch_or(1u << irq);
    std::lock_guard<std::mutex> lk(sIdleMutex);
    sIdleCv.notify_all();
}

void runIrqHandlers() {
    bool savedEn = sIntEnabled;
    sInIrq = true;
    sIntEnabled = false;
    u32 bits;
    while ((bits = sPendingIrq.exchange(0)) != 0) {
        for (int i = 0; i < PORT_IRQ_COUNT; i++) {
            if ((bits & (1u << i)) && sIrqHandlers[i]) {
                sIrqHandlers[i]();
            }
        }
    }
    sIntEnabled = savedEn;
    sInIrq = false;
}

void deliverIrqs() {
    if (sInIrq || !sIntEnabled || tlsSelf == nullptr || sPendingIrq.load(std::memory_order_relaxed) == 0) {
        return;
    }
    runIrqHandlers();
    if (RunQueueHint) {
        BOOL en = OSDisableInterrupts();
        SelectThread(FALSE);
        OSRestoreInterrupts(en);
    }
}

extern "C" void port_irq_poll(void) { deliverIrqs(); }

extern "C" {

BOOL OSDisableInterrupts(void) {
    BOOL prev = sIntEnabled;
    if (prev) {
        deliverIrqs();
    }
    sIntEnabled = false;
    return prev;
}

BOOL OSEnableInterrupts(void) {
    BOOL prev = sIntEnabled;
    sIntEnabled = true;
    deliverIrqs();
    return prev;
}

BOOL OSRestoreInterrupts(BOOL level) {
    BOOL prev = sIntEnabled;
    sIntEnabled = level ? true : false;
    if (sIntEnabled && !prev) {
        deliverIrqs();
    }
    return prev;
}

__OSInterruptHandler __OSSetInterruptHandler(__OSInterrupt interrupt, __OSInterruptHandler handler) {
    __OSInterruptHandler old = sSdkHandlers[interrupt & 31];
    sSdkHandlers[interrupt & 31] = handler;
    return old;
}

__OSInterruptHandler __OSGetInterruptHandler(s32 interrupt) { return sSdkHandlers[interrupt & 31]; }
OSInterruptMask __OSMaskInterrupts(OSInterruptMask mask) { return mask; }
OSInterruptMask __OSUnmaskInterrupts(OSInterruptMask mask) { return mask; }

}  // extern "C"

extern "C" void port_call_sdk_interrupt(int interrupt) {
    __OSInterruptHandler h = sSdkHandlers[interrupt & 31];
    if (h) {
        h((__OSInterrupt)interrupt, OSGetCurrentContext());
    }
}
