// Wii OS threads on host threads, with the Broadway's single-core semantics.
//
// The scheduling logic (32 priority run queues, priority inheritance through
// mutex queues, the Reschedule counter) is carried over from the SDK's
// OSThread.c.  What changes is the context switch: every OSThread runs on its
// own host thread, and exactly one of them holds the "CPU" at a time.  A
// switch hands the CPU to the next thread and blocks the current host thread
// until the scheduler picks it again.
//
// Interrupts (alarms, VI retrace, DVD/audio completion, GPU tokens) are raised
// by host threads and delivered on the CPU at safe points: when interrupts get
// re-enabled, at explicit port_irq_poll() calls, and whenever the CPU idles.
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <unwind.h>
#include <string.h>
#include <time.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "port/heap_routing.h"
#include "port/port.h"
#include "revolution/os.h"

extern "C" {
OSThread* __OSCurrentThread;
OSThreadQueue __OSActiveThreadQueue;
volatile OSContext* __OSFPUContext;
u32 __OSFpscrEnableBits;
}

extern "C" void __OSUnlockAllMutex(OSThread* thread);
extern "C" OSPriority __OSGetEffectivePriority(OSThread* thread);

// ---------------------------------------------------------------------------
// Scheduler state (only touched by the thread holding the CPU)
// ---------------------------------------------------------------------------
static volatile u32 RunQueueBits;
static OSThreadQueue RunQueue[32];
static volatile BOOL RunQueueHint;
static volatile s32 Reschedule;
static OSThread DefaultThread;
static OSContext IdleContext;
static OSContext* sCurrentContext;

#define EnqueueTail(queue, thread, link)                                                                                                             \
    do {                                                                                                                                             \
        OSThread* __prev = (queue)->tail;                                                                                                            \
        if (__prev == NULL)                                                                                                                          \
            (queue)->head = (thread);                                                                                                                \
        else                                                                                                                                         \
            __prev->link.next = (thread);                                                                                                            \
        (thread)->link.prev = __prev;                                                                                                                \
        (thread)->link.next = NULL;                                                                                                                  \
        (queue)->tail = (thread);                                                                                                                    \
    } while (0)

#define EnqueuePrio(queue, thread, link)                                                                                                             \
    do {                                                                                                                                             \
        OSThread* __prev;                                                                                                                            \
        OSThread* __next;                                                                                                                            \
        for (__next = (queue)->head; __next && __next->priority <= thread->priority; __next = __next->link.next)                                     \
            ;                                                                                                                                        \
        if (__next == NULL)                                                                                                                          \
            EnqueueTail(queue, thread, link);                                                                                                        \
        else {                                                                                                                                       \
            (thread)->link.next = __next;                                                                                                            \
            __prev = __next->link.prev;                                                                                                              \
            __next->link.prev = (thread);                                                                                                            \
            (thread)->link.prev = __prev;                                                                                                            \
            if (__prev == NULL)                                                                                                                      \
                (queue)->head = (thread);                                                                                                            \
            else                                                                                                                                     \
                __prev->link.next = (thread);                                                                                                        \
        }                                                                                                                                            \
    } while (0)

#define DequeueItem(queue, thread, link)                                                                                                             \
    do {                                                                                                                                             \
        OSThread* __next = (thread)->link.next;                                                                                                      \
        OSThread* __prev = (thread)->link.prev;                                                                                                      \
        if (__next == NULL)                                                                                                                          \
            (queue)->tail = __prev;                                                                                                                  \
        else                                                                                                                                         \
            __next->link.prev = __prev;                                                                                                              \
        if (__prev == NULL)                                                                                                                          \
            (queue)->head = __next;                                                                                                                  \
        else                                                                                                                                         \
            __prev->link.next = __next;                                                                                                              \
    } while (0)

#define DequeueHead(queue, thread, link)                                                                                                             \
    do {                                                                                                                                             \
        OSThread* __next;                                                                                                                            \
        (thread) = (queue)->head;                                                                                                                    \
        __next = (thread)->link.next;                                                                                                            \
        if (__next == NULL)                                                                                                                          \
            (queue)->tail = NULL;                                                                                                                    \
        else                                                                                                                                         \
            __next->link.prev = NULL;                                                                                                                \
        (queue)->head = __next;                                                                                                                      \
    } while (0)

#define IsSuspended(suspendCount) (0 < (suspendCount))

// ---------------------------------------------------------------------------
// Host side of each OSThread
// ---------------------------------------------------------------------------
struct HostThread {
    OSThread* thread = nullptr;
    void* (*func)(void*) = nullptr;
    void* param = nullptr;
    std::condition_variable cv;
    bool started = false;
    bool cancelled = false;
    bool intEnabled = true;  // saved MSR[EE] while switched out
    size_t stackSize = 0;
    pthread_t pt{};          // host thread (for diagnostics)
    bool hasPt = false;
};

static constexpr int kMaxThreads = 64;
static HostThread sHost[kMaxThreads];
static OSThread* sHostOwner[kMaxThreads];

static std::mutex sCpuMutex;
static OSThread* sCpuOwner;  // guarded by sCpuMutex
static thread_local OSThread* tlsSelf;
static thread_local int tlsHostAllocDepth;

static HostThread* hostOf(OSThread* t) {
    for (int i = 0; i < kMaxThreads; i++) {
        if (sHostOwner[i] == t) {
            return &sHost[i];
        }
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

// ---------------------------------------------------------------------------
// Virtual CPU interrupt state
// ---------------------------------------------------------------------------
static bool sIntEnabled = false;  // MSR[EE] of the running thread
static bool sInIrq = false;       // running interrupt handlers
static std::atomic<u32> sPendingIrq{0};
static std::mutex sIdleMutex;
static std::condition_variable sIdleCv;
static PortIrqHandler sIrqHandlers[PORT_IRQ_COUNT];

extern "C" void port_irq_set_handler(int irq, PortIrqHandler handler) { sIrqHandlers[irq] = handler; }

extern "C" void port_irq_raise(int irq) {
    sPendingIrq.fetch_or(1u << irq);
    std::lock_guard<std::mutex> lk(sIdleMutex);
    sIdleCv.notify_all();
}

static void runIrqHandlers() {
    // Called with the CPU held.  Handlers run with interrupts disabled and
    // may not switch threads (SelectThread checks sInIrq).
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

extern "C" OSThread* SelectThread(BOOL yield);

static void deliverIrqs() {
    if (sInIrq || !sIntEnabled || tlsSelf == nullptr || sPendingIrq.load(std::memory_order_relaxed) == 0) {
        return;
    }
    runIrqHandlers();
    // Returning from an exception: switch if a handler readied a better thread.
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
        deliverIrqs();  // a pending interrupt would have been taken before this point
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

}  // extern "C"

// ---------------------------------------------------------------------------
// Context switching
// ---------------------------------------------------------------------------
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

// Waits (with sCpuMutex held via lk) until `self` owns the CPU again.
static void waitForCpu(std::unique_lock<std::mutex>& lk, OSThread* self, HostThread* h) {
    h->cv.wait(lk, [&] { return sCpuOwner == self || h->cancelled; });
    if (h->cancelled && sCpuOwner != self) {
        lk.unlock();
        pthread_exit(nullptr);
    }
}

// Hands the CPU from the calling host thread to `next`.  Returns when the
// caller is scheduled again (immediately if next == caller), or never if the
// caller is exiting.
static void switchTo(OSThread* next, bool callerExiting) {
    OSThread* self = tlsSelf;
    __OSCurrentThread = next;
    sCurrentContext = &next->context;
    if (next == self) {
        return;
    }
    HostThread* hn = hostOf(next);
    HostThread* hs = (self && !callerExiting) ? hostOf(self) : nullptr;
    std::unique_lock<std::mutex> lk(sCpuMutex);
    if (hs) {
        hs->intEnabled = sIntEnabled;
    }
    sCpuOwner = next;
    if (!hn->started) {
        startHostThread(next, hn);
    } else {
        hn->cv.notify_one();
    }
    if (callerExiting || hs == nullptr) {
        return;
    }
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
    sIntEnabled = true;  // new threads start with MSR[EE] set
    void* ret = h->func(h->param);
    OSExitThread(ret);
    return nullptr;
}

// Runs interrupt handlers while no thread is runnable.
static void idleUntilRunnable() {
    while (RunQueueBits == 0) {
        {
            std::unique_lock<std::mutex> lk(sIdleMutex);
            sIdleCv.wait(lk, [] { return sPendingIrq.load() != 0; });
        }
        runIrqHandlers();
    }
}

// ---------------------------------------------------------------------------
// SDK scheduler logic
// ---------------------------------------------------------------------------
static void SetRun(OSThread* thread) {
    thread->queue = &RunQueue[thread->priority];
    EnqueueTail(thread->queue, thread, link);
    RunQueueBits |= 1u << (31 - thread->priority);
    RunQueueHint = TRUE;
}

static void UnsetRun(OSThread* thread) {
    OSThreadQueue* queue = thread->queue;
    DequeueItem(queue, thread, link);
    if (queue->head == 0) {
        RunQueueBits &= ~(1u << (31 - thread->priority));
    }
    thread->queue = 0;
}

extern "C" OSPriority __OSGetEffectivePriority(OSThread* thread) {
    OSPriority priority = thread->base;
    for (OSMutex* mutex = thread->queueMutex.head; mutex; mutex = mutex->link.next) {
        OSThread* blocked = mutex->queue.head;
        if (blocked != 0 && blocked->priority < priority) {
            priority = blocked->priority;
        }
    }
    return priority;
}

static OSThread* SetEffectivePriority(OSThread* thread, OSPriority priority) {
    switch (thread->state) {
    case OS_THREAD_STATE_READY:
        UnsetRun(thread);
        thread->priority = priority;
        SetRun(thread);
        break;
    case OS_THREAD_STATE_WAITING:
        DequeueItem(thread->queue, thread, link);
        thread->priority = priority;
        EnqueuePrio(thread->queue, thread, link);
        if (thread->mutex != 0) {
            return thread->mutex->thread;
        }
        break;
    case OS_THREAD_STATE_RUNNING:
        RunQueueHint = TRUE;
        thread->priority = priority;
        break;
    }
    return NULL;
}

static void UpdatePriority(OSThread* thread) {
    do {
        if (IsSuspended(thread->suspend)) {
            break;
        }
        OSPriority priority = __OSGetEffectivePriority(thread);
        if (thread->priority == priority) {
            break;
        }
        thread = SetEffectivePriority(thread, priority);
    } while (thread);
}

extern "C" void __OSPromoteThread(OSThread* thread, OSPriority priority) {
    do {
        if ((thread->suspend > 0) || thread->priority <= priority) {
            break;
        }
        thread = SetEffectivePriority(thread, priority);
    } while (thread);
}

static bool sExiting;  // set by OSExitThread for the calling thread's switch

extern "C" OSThread* SelectThread(BOOL yield) {
    if (Reschedule > 0 || sInIrq) {
        return 0;
    }
    OSThread* currentThread = __OSCurrentThread;
    if (currentThread != 0) {
        if (currentThread->state == OS_THREAD_STATE_RUNNING) {
            if (!yield) {
                OSPriority priority = __cntlzw(RunQueueBits);
                if (currentThread->priority <= priority) {
                    return 0;
                }
            }
            currentThread->state = OS_THREAD_STATE_READY;
            SetRun(currentThread);
        }
    }

    if (RunQueueBits == 0) {
        __OSCurrentThread = 0;
        sCurrentContext = &IdleContext;
        idleUntilRunnable();
    }

    RunQueueHint = FALSE;
    OSPriority priority = __cntlzw(RunQueueBits);
    OSThreadQueue* queue = &RunQueue[priority];
    OSThread* nextThread;
    DequeueHead(queue, nextThread, link);
    if (queue->head == 0) {
        RunQueueBits &= ~(1u << (31 - priority));
    }
    nextThread->queue = 0;
    nextThread->state = OS_THREAD_STATE_RUNNING;

    bool exiting = sExiting;
    sExiting = false;
    switchTo(nextThread, exiting);
    return nextThread;
}

static void __OSReschedule(void) {
    if (RunQueueHint) {
        SelectThread(FALSE);
    }
}

// ---------------------------------------------------------------------------
// Public thread API
// ---------------------------------------------------------------------------
extern "C" {

void OSInitThreadQueue(OSThreadQueue* queue) { queue->head = queue->tail = 0; }

OSThread* OSGetCurrentThread(void) { return __OSCurrentThread; }

BOOL OSIsThreadSuspended(OSThread* thread) { return (0 < thread->suspend) ? TRUE : FALSE; }

BOOL OSIsThreadTerminated(OSThread* thread) { return ((thread->state == OS_THREAD_STATE_MORIBUND) || (thread->state == 0)) ? TRUE : FALSE; }

static BOOL __OSIsThreadActive(OSThread* thread) {
    if (thread->state == 0) {
        return FALSE;
    }
    for (OSThread* active = __OSActiveThreadQueue.head; active; active = active->linkActive.next) {
        if (thread == active) {
            return TRUE;
        }
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
    if (priority < 0 || 31 < priority) {
        return FALSE;
    }
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
    if (stack != NULL && stackSize >= 4) {
        *(thread->stackEnd) = 0xDEADBABE;
    }
    thread->error = 0;
    thread->specific[0] = thread->specific[1] = 0;

    HostThread* h = hostOf(thread);
    h->func = func;
    h->param = param;
    h->started = false;
    h->cancelled = false;
    h->intEnabled = true;
    // Host code (GPU decode, audio mixing) runs on game threads too, so give
    // every thread a generous host stack regardless of the game's request.
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
    // Release the host slot while we still own the CPU; after the switch below
    // another thread runs and may reuse it.
    for (int i = 0; i < kMaxThreads; i++) {
        if (sHostOwner[i] == currentThread) {
            sHostOwner[i] = nullptr;
        }
    }
    sExiting = true;
    SelectThread(FALSE);
    // The host thread for an exited OSThread ends here.
    (void)enabled;
    pthread_exit(nullptr);
}

void OSCancelThread(OSThread* thread) {
    BOOL enabled = OSDisableInterrupts();
    switch (thread->state) {
    case OS_THREAD_STATE_READY:
        if (!IsSuspended(thread->suspend)) {
            UnsetRun(thread);
        }
        break;
    case OS_THREAD_STATE_RUNNING:
        RunQueueHint = TRUE;
        break;
    case OS_THREAD_STATE_WAITING:
        DequeueItem(thread->queue, thread, link);
        thread->queue = NULL;
        if (!IsSuspended(thread->suspend) && thread->mutex) {
            UpdatePriority(thread->mutex->thread);
        }
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
            if (sHostOwner[i] == thread) {
                sHostOwner[i] = nullptr;
            }
        }
        sExiting = true;
        SelectThread(FALSE);
        pthread_exit(nullptr);
    }
    {
        // Release the host thread parked waiting for the CPU.
        HostThread* h = hostOf(thread);
        std::lock_guard<std::mutex> lk(sCpuMutex);
        h->cancelled = true;
        h->cv.notify_one();
        for (int i = 0; i < kMaxThreads; i++) {
            if (sHostOwner[i] == thread) {
                sHostOwner[i] = nullptr;
            }
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
        if (val) {
            *val = thread->value;
        }
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

s32 OSResumeThread(OSThread* thread) {
    BOOL enabled = OSDisableInterrupts();
    s32 suspendCount = thread->suspend--;
    if (thread->suspend < 0) {
        thread->suspend = 0;
    } else if (thread->suspend == 0) {
        switch (thread->state) {
        case OS_THREAD_STATE_READY:
            thread->priority = __OSGetEffectivePriority(thread);
            SetRun(thread);
            break;
        case OS_THREAD_STATE_WAITING:
            DequeueItem(thread->queue, thread, link);
            thread->priority = __OSGetEffectivePriority(thread);
            EnqueuePrio(thread->queue, thread, link);
            if (thread->mutex) {
                UpdatePriority(thread->mutex->thread);
            }
            break;
        }
        __OSReschedule();
    }
    OSRestoreInterrupts(enabled);
    return suspendCount;
}

s32 OSSuspendThread(OSThread* thread) {
    BOOL enabled = OSDisableInterrupts();
    s32 suspendCount = thread->suspend++;
    if (suspendCount == 0) {
        switch (thread->state) {
        case OS_THREAD_STATE_RUNNING:
            RunQueueHint = TRUE;
            thread->state = OS_THREAD_STATE_READY;
            break;
        case OS_THREAD_STATE_READY:
            UnsetRun(thread);
            break;
        case OS_THREAD_STATE_WAITING:
            DequeueItem(thread->queue, thread, link);
            thread->priority = 32;
            EnqueueTail(thread->queue, thread, link);
            if (thread->mutex) {
                UpdatePriority(thread->mutex->thread);
            }
            break;
        }
        __OSReschedule();
    }
    OSRestoreInterrupts(enabled);
    return suspendCount;
}

void OSSleepThread(OSThreadQueue* queue) {
    BOOL enabled = OSDisableInterrupts();
    OSThread* currentThread = OSGetCurrentThread();
    currentThread->state = OS_THREAD_STATE_WAITING;
    currentThread->queue = queue;
    EnqueuePrio(queue, currentThread, link);
    RunQueueHint = TRUE;
    __OSReschedule();
    OSRestoreInterrupts(enabled);
}

void OSWakeupThread(OSThreadQueue* queue) {
    BOOL enabled = OSDisableInterrupts();
    while (queue->head) {
        OSThread* thread;
        DequeueHead(queue, thread, link);
        thread->state = OS_THREAD_STATE_READY;
        if (!IsSuspended(thread->suspend)) {
            SetRun(thread);
        }
    }
    __OSReschedule();
    OSRestoreInterrupts(enabled);
}

BOOL OSSetThreadPriority(OSThread* thread, OSPriority priority) {
    if (priority < 0 || priority > 31) {
        return FALSE;
    }
    BOOL enabled = OSDisableInterrupts();
    if (thread->base != priority) {
        thread->base = priority;
        UpdatePriority(thread);
        __OSReschedule();
    }
    OSRestoreInterrupts(enabled);
    return TRUE;
}

OSPriority OSGetThreadPriority(OSThread* thread) { return thread->base; }

static void SleepAlarmHandler(OSAlarm* alarm, OSContext*) { OSResumeThread((OSThread*)OSGetAlarmUserData(alarm)); }

void OSSleepTicks(OSTime tick) {
    BOOL enabled = OSDisableInterrupts();
    OSThread* current = OSGetCurrentThread();
    if (current == NULL) {
        OSRestoreInterrupts(enabled);
        return;
    }
    OSAlarm sleepAlarm;
    OSCreateAlarm(&sleepAlarm);
    OSSetAlarmTag(&sleepAlarm, (u32)(uintptr_t)current);
    OSSetAlarmUserData(&sleepAlarm, (void*)current);
    OSSetAlarm(&sleepAlarm, tick, SleepAlarmHandler);
    OSSuspendThread(current);
    OSCancelAlarm(&sleepAlarm);
    OSRestoreInterrupts(enabled);
}

// ---------------------------------------------------------------------------
// Contexts (register state is meaningless on the host; keep the bookkeeping)
// ---------------------------------------------------------------------------
OSContext* OSGetCurrentContext(void) { return sCurrentContext ? sCurrentContext : &IdleContext; }
void OSSetCurrentContext(OSContext* context) { sCurrentContext = context; }
void OSClearContext(OSContext* context) { (void)context; }
void OSFillFPUContext(OSContext* context) { (void)context; }
u32 OSGetStackPointer(void) { return (u32)(uintptr_t)__builtin_frame_address(0); }

// SDK interrupt controller hooks: the GX library registers PE handlers here.
static __OSInterruptHandler sSdkHandlers[32];
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

// ---------------------------------------------------------------------------
// Heap routing (see port/heap_routing.h)
// ---------------------------------------------------------------------------
extern "C" int port_is_game_thread(void) { return tlsSelf != nullptr; }
extern "C" int port_use_game_heap(void) { return tlsSelf != nullptr && tlsHostAllocDepth == 0; }
extern "C" void port_host_alloc_scope_enter(void) { tlsHostAllocDepth++; }
extern "C" void port_host_alloc_scope_leave(void) { tlsHostAllocDepth--; }
extern "C" int port_is_game_heap_ptr(const void* p) { return port_is_game_memory(p); }
extern "C" void* port_host_alloc(size_t size, int align) {
    if (align < 16) {
        align = 16;
    }
    void* p = nullptr;
    if (posix_memalign(&p, (size_t)align, size ? size : 1) != 0) {
        return nullptr;
    }
    return p;
}
extern "C" void port_host_free(void* p) { free(p); }

// ---------------------------------------------------------------------------
// Diagnostics: emulated thread states and native backtraces
// ---------------------------------------------------------------------------
static std::atomic<int> sBtDone;
static uintptr_t sBtPcs[40];
static int sBtCount;

static _Unwind_Reason_Code btCallback(struct _Unwind_Context* ctx, void*) {
    uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc && sBtCount < 40) {
        sBtPcs[sBtCount++] = pc;
    }
    return _URC_NO_REASON;
}

static void btSignal(int) {
    sBtCount = 0;
    _Unwind_Backtrace(btCallback, nullptr);
    sBtDone.store(1);
}

extern "C" void port_debug_dump_threads(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = btSignal;
    sigaction(SIGURG, &sa, nullptr);
    port_log("threads: cpu owner %p, pending irqs %08x, int enabled %d", (void*)sCpuOwner, sPendingIrq.load(), (int)sIntEnabled);
    for (int i = 0; i < kMaxThreads; i++) {
        OSThread* t = sHostOwner[i];
        if (!t) {
            continue;
        }
        HostThread& h = sHost[i];
        port_log("thread %p prio %d/%d state %d suspend %d queue %p mutex %p%s", (void*)t, t->priority, t->base, t->state, t->suspend,
                 (void*)t->queue, (void*)t->mutex, t == sCpuOwner ? " [CPU]" : "");
        if (!h.hasPt) {
            continue;
        }
        sBtDone.store(0);
        if (pthread_kill(h.pt, SIGURG) != 0) {
            continue;
        }
        for (int w = 0; w < 200 && !sBtDone.load(); w++) {
            usleep(1000);
        }
        char line[1024];
        int n = 0;
        for (int k = 0; k < sBtCount && n < 1000; k++) {
            uintptr_t pc = sBtPcs[k];
            if (pc >= 0x98000000u && pc < 0xA0000000u) {
                n += snprintf(line + n, sizeof(line) - n, " g:%lx", (unsigned long)(pc - 0x98000000u));
            } else {
                n += snprintf(line + n, sizeof(line) - n, " %lx", (unsigned long)pc);
            }
        }
        port_log("  bt:%s", line);
    }
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
static void (*sBootEntry)(void);

static void* defaultThreadMain(void*) {
    port_pin_thread_to_performance_cores();
    OSThread* thread = &DefaultThread;
    tlsSelf = thread;
    hostOf(thread)->pt = pthread_self();
    hostOf(thread)->hasPt = true;
    __OSCurrentThread = thread;
    sCurrentContext = &thread->context;
    {
        std::lock_guard<std::mutex> lk(sCpuMutex);
        sCpuOwner = thread;
    }
    sIntEnabled = true;
    sBootEntry();
    port_fatal("game main returned");
    return nullptr;
}

extern "C" void port_os_boot(void (*entry)(void)) {
    OSThread* thread = &DefaultThread;
    thread->state = OS_THREAD_STATE_RUNNING;
    thread->attr = 1;
    thread->priority = thread->base = 16;
    thread->suspend = 0;
    thread->value = (void*)-1;
    thread->mutex = NULL;
    OSInitThreadQueue(&thread->queueJoin);
    thread->queueMutex.head = thread->queueMutex.tail = NULL;
    __OSFPUContext = &thread->context;

    const size_t kMainStack = 16u << 20;
    u8* stack = (u8*)port_low_alloc(kMainStack);
    thread->stackBase = stack + kMainStack;
    thread->stackEnd = (u32*)stack;
    *(thread->stackEnd) = 0xDEADBABE;

    RunQueueBits = 0;
    RunQueueHint = FALSE;
    for (int prio = 0; prio <= 31; prio++) {
        OSInitThreadQueue(&RunQueue[prio]);
    }
    OSInitThreadQueue(&__OSActiveThreadQueue);
    EnqueueTail(&__OSActiveThreadQueue, thread, linkActive);
    Reschedule = 0;

    HostThread* h = hostOf(thread);
    h->started = true;
    sBootEntry = entry;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack, kMainStack);
    pthread_t pt;
    if (pthread_create(&pt, &attr, defaultThreadMain, nullptr) != 0) {
        port_fatal("failed to start the game thread");
    }
    pthread_attr_destroy(&attr);
    pthread_detach(pt);
}
