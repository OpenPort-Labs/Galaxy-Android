#include "os_internal.h"

#include <pthread.h>
#include <string.h>

extern "C" {
OSThread* __OSCurrentThread;
OSThreadQueue __OSActiveThreadQueue;
volatile OSContext* __OSFPUContext;
u32 __OSFpscrEnableBits;
}

extern "C" void __OSUnlockAllMutex(OSThread* thread);
void UpdatePriority(OSThread* thread);
void __OSReschedule(void);

HostThread sHost[kMaxThreads];
OSThread* sHostOwner[kMaxThreads];
std::mutex sCpuMutex;
OSThread* sCpuOwner = nullptr;
thread_local OSThread* tlsSelf = nullptr;

OSThread DefaultThread;
OSContext* sCurrentContext = nullptr;

HostThread* hostOf(OSThread* t) {
    for (int i = 0; i < kMaxThreads; i++) {
        if (sHostOwner[i] == t) return &sHost[i];
    }
    for (int i = 0; i < kMaxThreads; i++) {
        if (sHostOwner[i] == nullptr) {
            sHostOwner[i] = t;
            sHost[i].thread = t;
            sHost[i].func = nullptr;
            sHost[i].param = nullptr;
            sHost[i].started = false;
            sHost[i].cancelled = false;
            sHost[i].intEnabled = true;
            return &sHost[i];
        }
    }
    port_fatal("too many OSThreads");
}

static void* hostThreadMain(void* arg);

static void startHostThread(OSThread* t, HostThread* h) {
    size_t stackSize = h->stackSize ? h->stackSize : (2u << 20);
    void* stack = port_low_alloc(stackSize);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack, stackSize);
    pthread_t pt;
    if (pthread_create(&pt, &attr, hostThreadMain, t) != 0) {
        port_fatal("pthread_create failed");
    }
    pthread_attr_destroy(&attr);
    pthread_detach(pt);
    h->started = true;
}

static void waitForCpu(std::unique_lock<std::mutex>& lk, OSThread* self, HostThread* h) {
    h->cv.wait(lk, [&] { return sCpuOwner == self || h->cancelled; });
    if (h->cancelled && sCpuOwner != self) {
        lk.unlock();
        pthread_exit(nullptr);
    }
}

void switchTo(OSThread* next, bool callerExiting) {
    OSThread* self = tlsSelf;
    __OSCurrentThread = next;
    sCurrentContext = &next->context;
    if (next == self) return;
    HostThread* hn = hostOf(next);
    HostThread* hs = (self && !callerExiting) ? hostOf(self) : nullptr;
    std::unique_lock<std::mutex> lk(sCpuMutex);
    if (hs) hs->intEnabled = sIntEnabled;
    sCpuOwner = next;
    if (!hn->started) {
        startHostThread(next, hn);
    } else {
        hn->cv.notify_one();
    }
    if (callerExiting || hs == nullptr) return;
    waitForCpu(lk, self, hs);
    lk.unlock();
    sIntEnabled = hs->intEnabled;
    __OSCurrentThread = self;
    sCurrentContext = &self->context;
}

static void* hostThreadMain(void* arg) {
    OSThread* self = (OSThread*)arg;
    tlsSelf = self;
    HostThread* h = hostOf(self);
    h->pt = pthread_self();
    h->hasPt = true;
    {
        std::unique_lock<std::mutex> lk(sCpuMutex);
        waitForCpu(lk, self, h);
    }
    __OSCurrentThread = self;
    sCurrentContext = &self->context;
    sIntEnabled = true;
    void* ret = h->func(h->param);
    OSExitThread(ret);
    return nullptr;
}

extern "C" {

void OSInitThreadQueue(OSThreadQueue* queue) { queue->head = queue->tail = 0; }
OSThread* OSGetCurrentThread(void) { return __OSCurrentThread; }
BOOL OSIsThreadSuspended(OSThread* thread) { return (0 < thread->suspend) ? TRUE : FALSE; }
BOOL OSIsThreadTerminated(OSThread* thread) { return ((thread->state == OS_THREAD_STATE_MORIBUND) || (thread->state == 0)) ? TRUE : FALSE; }

static BOOL __OSIsThreadActive(OSThread* thread) {
    if (thread->state == 0) return FALSE;
    for (OSThread* active = __OSActiveThreadQueue.head; active; active = active->linkActive.next) {
        if (thread == active) return TRUE;
    }
    return FALSE;
}

s32 OSDisableScheduler(void) {
    BOOL enabled = OSDisableInterrupts();
    s32 count = Reschedule++;
    OSRestoreInterrupts(enabled);
    return count;
}

s32 OSEnableScheduler(void) {
    BOOL enabled = OSDisableInterrupts();
    s32 count = Reschedule--;
    OSRestoreInterrupts(enabled);
    return count;
}

void OSYieldThread(void) {
    BOOL enabled = OSDisableInterrupts();
    SelectThread(TRUE);
    OSRestoreInterrupts(enabled);
}

BOOL OSCreateThread(OSThread* thread, void* (*func)(void*), void* param, void* stack, u32 stackSize, OSPriority priority, u16 attr) {
    if (priority < 0 || 31 < priority) return FALSE;
    thread->state = OS_THREAD_STATE_READY;
    thread->attr = (u16)(attr & 1);
    thread->priority = thread->base = priority;
    thread->suspend = 1;
    thread->value = (void*)-1;
    thread->mutex = NULL;
    OSInitThreadQueue(&thread->queueJoin);
    thread->queueMutex.head = thread->queueMutex.tail = NULL;
    memset(&thread->context, 0, sizeof(thread->context));
    thread->stackBase = (u8*)stack;
    thread->stackEnd = (u32*)((u8*)stack - stackSize);
    if (stack != NULL && stackSize >= 4) *(thread->stackEnd) = 0xDEADBABE;
    thread->error = 0;
    thread->specific[0] = thread->specific[1] = 0;

    HostThread* h = hostOf(thread);
    h->func = func;
    h->param = param;
    h->started = false;
    h->cancelled = false;
    h->intEnabled = true;
    h->stackSize = (size_t)(stackSize > 0x8000 ? 4u << 20 : 2u << 20);

    BOOL enabled = OSDisableInterrupts();
    EnqueueTail(&__OSActiveThreadQueue, thread, linkActive);
    OSRestoreInterrupts(enabled);
    return TRUE;
}

void OSExitThread(void* val) {
    BOOL enabled = OSDisableInterrupts();
    OSThread* currentThread = OSGetCurrentThread();
    if (currentThread->attr & 1) {
        DequeueItem(&__OSActiveThreadQueue, currentThread, linkActive);
        currentThread->state = 0;
    } else {
        currentThread->state = OS_THREAD_STATE_MORIBUND;
        currentThread->value = val;
    }
    __OSUnlockAllMutex(currentThread);
    OSWakeupThread(&currentThread->queueJoin);
    RunQueueHint = TRUE;
    for (int i = 0; i < kMaxThreads; i++) {
        if (sHostOwner[i] == currentThread) sHostOwner[i] = nullptr;
    }
    sExiting = true;
    SelectThread(FALSE);
    (void)enabled;
    pthread_exit(nullptr);
}

void OSCancelThread(OSThread* thread) {
    BOOL enabled = OSDisableInterrupts();
    switch (thread->state) {
    case OS_THREAD_STATE_READY:
        if (!IsSuspended(thread->suspend)) UnsetRun(thread);
        break;
    case OS_THREAD_STATE_RUNNING:
        RunQueueHint = TRUE;
        break;
    case OS_THREAD_STATE_WAITING:
        DequeueItem(thread->queue, thread, link);
        thread->queue = NULL;
        if (!IsSuspended(thread->suspend) && thread->mutex) UpdatePriority(thread->mutex->thread);
        break;
    default:
        OSRestoreInterrupts(enabled);
        return;
    }
    if (thread->attr & 1) {
        DequeueItem(&__OSActiveThreadQueue, thread, linkActive);
        thread->state = 0;
    } else {
        thread->state = OS_THREAD_STATE_MORIBUND;
    }
    __OSUnlockAllMutex(thread);
    OSWakeupThread(&thread->queueJoin);

    if (thread == OSGetCurrentThread()) {
        for (int i = 0; i < kMaxThreads; i++) {
            if (sHostOwner[i] == thread) sHostOwner[i] = nullptr;
        }
        sExiting = true;
        SelectThread(FALSE);
        pthread_exit(nullptr);
    }
    {
        HostThread* h = hostOf(thread);
        std::lock_guard<std::mutex> lk(sCpuMutex);
        h->cancelled = true;
        h->cv.notify_one();
        for (int i = 0; i < kMaxThreads; i++) {
            if (sHostOwner[i] == thread) sHostOwner[i] = nullptr;
        }
    }
    __OSReschedule();
    OSRestoreInterrupts(enabled);
}

BOOL OSJoinThread(OSThread* thread, void** val) {
    BOOL enabled = OSDisableInterrupts();
    if (!(thread->attr & 1) && thread->state != OS_THREAD_STATE_MORIBUND && thread->queueJoin.head == NULL) {
        OSSleepThread(&thread->queueJoin);
        if (!__OSIsThreadActive(thread)) {
            OSRestoreInterrupts(enabled);
            return FALSE;
        }
    }
    if (((volatile OSThread*)thread)->state == OS_THREAD_STATE_MORIBUND) {
        if (val) *val = thread->value;
        DequeueItem(&__OSActiveThreadQueue, thread, linkActive);
        thread->state = 0;
        OSRestoreInterrupts(enabled);
        return TRUE;
    }
    OSRestoreInterrupts(enabled);
    return FALSE;
}

void OSDetachThread(OSThread* thread) {
    BOOL enabled = OSDisableInterrupts();
    thread->attr |= 1;
    if (thread->state == OS_THREAD_STATE_MORIBUND) {
        DequeueItem(&__OSActiveThreadQueue, thread, linkActive);
        thread->state = 0;
    }
    OSWakeupThread(&thread->queueJoin);
    OSRestoreInterrupts(enabled);
}

}  // extern "C"

