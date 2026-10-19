#include "os_internal.h"

volatile u32 RunQueueBits = 0;
OSThreadQueue RunQueue[32];
volatile BOOL RunQueueHint = FALSE;
volatile s32 Reschedule = 0;
OSContext IdleContext;
bool sExiting = false;

void idleUntilRunnable() {
    while (RunQueueBits == 0) {
        {
            std::unique_lock<std::mutex> lk(sIdleMutex);
            sIdleCv.wait(lk, [] { return sPendingIrq.load() != 0; });
        }
        runIrqHandlers();
    }
}

void SetRun(OSThread* thread) {
    thread->queue = &RunQueue[thread->priority];
    EnqueueTail(thread->queue, thread, link);
    RunQueueBits |= 1u << (31 - thread->priority);
    RunQueueHint = TRUE;
}

void UnsetRun(OSThread* thread) {
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

void UpdatePriority(OSThread* thread) {
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

void __OSReschedule(void) {
    if (RunQueueHint) {
        SelectThread(FALSE);
    }
}
