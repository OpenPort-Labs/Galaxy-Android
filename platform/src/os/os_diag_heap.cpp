#include "os_internal.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <unwind.h>

thread_local int tlsHostAllocDepth = 0;

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
