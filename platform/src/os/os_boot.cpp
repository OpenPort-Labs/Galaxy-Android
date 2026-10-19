#include "os_internal.h"

#include <pthread.h>

extern "C" {
extern OSThread* __OSCurrentThread;
extern OSThreadQueue __OSActiveThreadQueue;
extern volatile OSContext* __OSFPUContext;
extern u32 __OSFpscrEnableBits;
}

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
