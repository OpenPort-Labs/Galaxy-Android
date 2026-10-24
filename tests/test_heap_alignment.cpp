// Host test for the global `new` contract on 64-bit builds.
//
// The decompiled game asked JKRHeap for 4-byte alignment; the port requests
// __STDCPP_DEFAULT_NEW_ALIGNMENT__ instead.  This drives the real operator new
// through the real JKRExpHeap so a regression back to 4-byte alignment fails.
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include "JSystem/JUtility/JUTException.hpp"
#include <malloc.h>
#include <new>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#endif

static int gUseGameHeap;
static const unsigned char* gHeapBegin;
static const unsigned char* gHeapEnd;
static int gFailures;

extern "C" int port_use_game_heap(void) {
    return gUseGameHeap;
}

extern "C" int port_is_game_heap_ptr(const void* p) {
    const unsigned char* q = static_cast< const unsigned char* >(p);
    return q >= gHeapBegin && q < gHeapEnd;
}

extern "C" void* port_host_alloc(size_t size, int align) {
    if (align < 16) {
        align = 16;
    }
#ifdef _WIN32
    return _aligned_malloc(size ? size : 1, static_cast< size_t >(align));
#else
    void* p = nullptr;
    return posix_memalign(&p, static_cast< size_t >(align), size ? size : 1) == 0 ? p : nullptr;
#endif
}

extern "C" void port_host_free(void* p) {
#ifdef _WIN32
    _aligned_free(p);
#else
    free(p);
#endif
}

extern "C" void port_host_alloc_scope_enter(void) {}
extern "C" void port_host_alloc_scope_leave(void) {}

extern "C" void JUTWarningConsole_f(const char*, ...) {}
extern "C" void JUTWarningConsole(const char*) {}
extern "C" void JUTReportConsole_f(const char*, ...) {}
extern "C" void JUTReportConsole(const char*) {}
void JUTException::panic_f(const char*, int, const char*, ...) {}

// The host build has no real interrupt/thread/arena state; allocation only
// needs these entry points to exist.
extern "C" void OSInitMutex(OSMutex*) {}
extern "C" void OSLockMutex(OSMutex*) {}
extern "C" void OSUnlockMutex(OSMutex*) {}
extern "C" void OSReport(const char*, ...) {}
extern "C" void* OSGetArenaLo(void) {
    return nullptr;
}
extern "C" void* OSGetArenaHi(void) {
    return nullptr;
}
extern "C" void* OSInitAlloc(void*, void*, int) {
    return nullptr;
}
extern "C" void OSSetArenaLo(void*) {}
extern "C" void OSSetArenaHi(void*) {}

u32 JKRHeap::getMaxAllocatableSize(int) {
    return 0;
}

// JKRExpHeap stores block links as 32-bit guest addresses, so the heap backing
// store has to live below 4 GiB just like the reserved guest window.
static unsigned char* reserveLowMemory(size_t size) {
#ifdef _WIN32
    for (uintptr_t base = 0x20000000; base < 0x70000000; base += 0x01000000) {
        void* p = VirtualAlloc(reinterpret_cast< void* >(base), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p != nullptr) {
            return static_cast< unsigned char* >(p);
        }
    }
    return nullptr;
#else
    void* p = mmap(reinterpret_cast< void* >(0x20000000), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    return p == MAP_FAILED ? nullptr : static_cast< unsigned char* >(p);
#endif
}

static void expectAligned(const char* what, void* p, size_t align) {
    if (p == nullptr) {
        printf("FAIL %s: allocation returned null\n", what);
        gFailures++;
        return;
    }
    if (reinterpret_cast< uintptr_t >(p) % align != 0) {
        printf("FAIL %s: %p is not %u-byte aligned\n", what, p, (unsigned)align);
        gFailures++;
    }
}

int main() {
    const size_t heapSize = 512 * 1024;
    unsigned char* sHeap = reserveLowMemory(heapSize);
    alignas(64) static unsigned char sHeapStorage[sizeof(JKRExpHeap)];
    if (sHeap == nullptr) {
        printf("FAIL: could not reserve low memory for the test heap\n");
        return 1;
    }
    gHeapBegin = sHeap;
    gHeapEnd = sHeap + heapSize;

    // Placement-new so ~JKRExpHeap (which expects a fully rooted heap) never runs.
    JKRExpHeap* heap = new (sHeapStorage) JKRExpHeap(sHeap, static_cast< u32 >(heapSize), nullptr, false);
    JKRHeap::sRootHeap = heap;
    JKRHeap::sCurrentHeap = heap;
    gUseGameHeap = 1;

    static const size_t sizes[] = {0, 1, 2, 4, 5, 8, 9, 16, 17, 24, 31, 32, 33, 63, 64, 100, 255, 256, 1000, 4096};
    void* ptrs[sizeof(sizes) / sizeof(sizes[0])];
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        ptrs[i] = ::operator new(sizes[i]);
        expectAligned("new", ptrs[i], 16);
        if (ptrs[i] != nullptr) {
            memset(ptrs[i], 0xA5, sizes[i]);
        }
    }

    void* arrayPtr = ::operator new[](37);
    expectAligned("new[]", arrayPtr, 16);

    void* heapPtr = ::operator new(24, heap);
    expectAligned("new(heap)", heapPtr, 16);

    void* aligned64 = ::operator new(40, 64);
    expectAligned("new(64)", aligned64, 64);

    void* tail64 = ::operator new(40, -64);
    expectAligned("new(-64)", tail64, 64);

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        ::operator delete(ptrs[i]);
    }
    ::operator delete(arrayPtr);
    ::operator delete(heapPtr);
    ::operator delete(aligned64);
    ::operator delete(tail64);

    // Reuse the freed space: every block must still come back aligned.
    for (int i = 0; i < 200; i++) {
        void* p = ::operator new(static_cast< size_t >(i * 7 + 1));
        expectAligned("new(reuse)", p, 16);
        ::operator delete(p);
    }

    // Explicit legacy 4-byte requests must keep working on the same heap.
    void* legacy = JKRHeap::alloc(24, 4, nullptr);
    if (legacy == nullptr) {
        printf("FAIL JKRHeap::alloc(24, 4) returned null\n");
        gFailures++;
    }
    JKRHeap::free(legacy, nullptr);

    // Host-side allocations bypass the game heap and must also be aligned.
    gUseGameHeap = 0;
    void* hostPtr = ::operator new(24);
    expectAligned("host new", hostPtr, 16);
    ::operator delete(hostPtr);
    void* host64 = ::operator new(24, 64);
    expectAligned("host new(64)", host64, 64);
    ::operator delete(host64);

    if (gFailures == 0) {
        printf("PASSED (0 failures)\n");
        return 0;
    }
    printf("FAILED (%d failures)\n", gFailures);
    return 1;
}
