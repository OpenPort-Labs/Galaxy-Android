#pragma once

#include <pthread.h>
#include <stdint.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "port/port.h"
#include "revolution/os.h"

struct HostThread {
    OSThread* thread = nullptr;
    void* (*func)(void*) = nullptr;
    void* param = nullptr;
    std::condition_variable cv;
    bool started = false;
    bool cancelled = false;
    bool intEnabled = true;
    size_t stackSize = 0;
    pthread_t pt{};
    bool hasPt = false;
};

constexpr int kMaxThreads = 64;
extern HostThread sHost[kMaxThreads];
extern OSThread* sHostOwner[kMaxThreads];

extern std::mutex sCpuMutex;
extern OSThread* sCpuOwner;
extern thread_local OSThread* tlsSelf;

extern bool sIntEnabled;
extern bool sInIrq;
extern std::atomic<u32> sPendingIrq;
extern std::mutex sIdleMutex;
extern std::condition_variable sIdleCv;

extern volatile u32 RunQueueBits;
extern OSThreadQueue RunQueue[32];
extern volatile BOOL RunQueueHint;
extern volatile s32 Reschedule;
extern OSThread DefaultThread;
extern OSContext IdleContext;
extern OSContext* sCurrentContext;
extern bool sExiting;

HostThread* hostOf(OSThread* t);
void runIrqHandlers();
void deliverIrqs();
void idleUntilRunnable();
void switchTo(OSThread* next, bool callerExiting);
void SetRun(OSThread* thread);
void UnsetRun(OSThread* thread);
void UpdatePriority(OSThread* thread);
void __OSReschedule(void);
extern "C" OSThread* SelectThread(BOOL yield);
extern "C" OSThread* __OSCurrentThread;
extern "C" OSPriority __OSGetEffectivePriority(OSThread* thread);


#define EnqueueTail(queue, thread, link)                                           \
    do {                                                                           \
        OSThread* __prev = (queue)->tail;                                          \
        if (__prev == NULL)                                                        \
            (queue)->head = (thread);                                              \
        else                                                                       \
            __prev->link.next = (thread);                                          \
        (thread)->link.prev = __prev;                                              \
        (thread)->link.next = NULL;                                                \
        (queue)->tail = (thread);                                                  \
    } while (0)

#define EnqueuePrio(queue, thread, link)                                           \
    do {                                                                           \
        OSThread* __prev;                                                          \
        OSThread* __next;                                                          \
        for (__next = (queue)->head; __next && __next->priority <= thread->priority; __next = __next->link.next) \
            ;                                                                      \
        if (__next == NULL)                                                        \
            EnqueueTail(queue, thread, link);                                      \
        else {                                                                     \
            (thread)->link.next = __next;                                          \
            __prev = __next->link.prev;                                            \
            __next->link.prev = (thread);                                          \
            (thread)->link.prev = __prev;                                          \
            if (__prev == NULL)                                                    \
                (queue)->head = (thread);                                          \
            else                                                                   \
                __prev->link.next = (thread);                                      \
        }                                                                          \
    } while (0)

#define DequeueItem(queue, thread, link)                                           \
    do {                                                                           \
        OSThread* __next = (thread)->link.next;                                    \
        OSThread* __prev = (thread)->link.prev;                                    \
        if (__next == NULL)                                                        \
            (queue)->tail = __prev;                                                \
        else                                                                       \
            __next->link.prev = __prev;                                            \
        if (__prev == NULL)                                                        \
            (queue)->head = __next;                                                \
        else                                                                       \
            __prev->link.next = __next;                                            \
    } while (0)

#define DequeueHead(queue, thread, link)                                           \
    do {                                                                           \
        OSThread* __next;                                                          \
        (thread) = (queue)->head;                                                  \
        __next = (thread)->link.next;                                              \
        if (__next == NULL)                                                        \
            (queue)->tail = NULL;                                                  \
        else                                                                       \
            __next->link.prev = NULL;                                              \
        (queue)->head = __next;                                                    \
    } while (0)

#define IsSuspended(suspendCount) (0 < (suspendCount))
