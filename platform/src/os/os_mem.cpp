// Wii-compatible address space for the game.
//
// MEM1/MEM2 are mapped at their console virtual addresses (0x80000000 /
// 0x90000000) with uncached mirrors (0xC0000000 / 0xD0000000) that alias the
// same pages, so game code that does address arithmetic on "physical" or
// "uncached" addresses keeps working, and every game pointer fits in 32 bits.
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>

#include "port/guest_layout.h"
#include "port/port.h"
#include "revolution/os.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#if defined(__ANDROID__) || defined(__linux__)
#include <sys/syscall.h>
static int makeSharedFd(const char* name, size_t size) {
    int fd = (int)syscall(__NR_memfd_create, name, 1u /* MFD_CLOEXEC */);
    if (fd < 0) {
        port_fatal("memfd_create failed: %s", strerror(errno));
    }
    if (ftruncate(fd, (off_t)size) != 0) {
        port_fatal("ftruncate(%zu) failed: %s", size, strerror(errno));
    }
    return fd;
}
#endif

// Address window reserved (PROT_NONE) by the launcher before anything else
// could claim it; mappings inside it replace the reservation.
static uintptr_t sWindowBase, sWindowEnd;

extern "C" void port_mem_set_reserved_window(uintptr_t base, size_t size) {
    sWindowBase = base;
    sWindowEnd = base + size;
}

static void* mapAt(uintptr_t addr, size_t size, int fd) {
    bool inWindow = addr >= sWindowBase && addr + size <= sWindowEnd;
    int flags = (fd >= 0 ? MAP_SHARED : (MAP_PRIVATE | MAP_ANONYMOUS)) | (inWindow ? MAP_FIXED : MAP_FIXED_NOREPLACE);
    void* p = mmap((void*)addr, size, PROT_READ | PROT_WRITE, flags, fd, 0);
    if (p == MAP_FAILED) {
        port_fatal("mmap at 0x%08lx (+0x%zx) failed: %s", (unsigned long)addr, size, strerror(errno));
    }
    if ((uintptr_t)p != addr) {
        port_fatal("mmap at 0x%08lx landed at %p (address range in use)", (unsigned long)addr, p);
    }
    return p;
}

static bool sMemReady = false;

// ---------------------------------------------------------------------------
// Arena state (OSGetArenaLo/Hi and friends)
// ---------------------------------------------------------------------------
static void* sArenaLo;
static void* sArenaHi;
static void* sMEM2ArenaLo;
static void* sMEM2ArenaHi;

static inline void storeBE32(uintptr_t addr, u32 v) {
    // Low-memory OS globals are read by game code as native u32.
    *(volatile u32*)addr = v;
}

extern "C" int port_mem_init(void) {
    if (sMemReady) {
        return 1;
    }
    if (!guest_layout_validate_definitions()) {
        port_fatal("guest memory layout definitions failed validation");
    }
    int fd1 = makeSharedFd("wii-mem1", PORT_MEM1_SIZE);
    mapAt(PORT_MEM1_BASE, PORT_MEM1_SIZE, fd1);
    mapAt(PORT_MEM1_UNCACHED, PORT_MEM1_SIZE, fd1);
    close(fd1);

    int fd2 = makeSharedFd("wii-mem2", PORT_MEM2_SIZE);
    mapAt(PORT_MEM2_BASE, PORT_MEM2_SIZE, fd2);
    mapAt(PORT_MEM2_UNCACHED, PORT_MEM2_SIZE, fd2);
    close(fd2);

    // Locked-cache scratchpad (16 KiB on hardware).
    mapAt(PORT_LOCKED_CACHE, 0x4000, -1);

    // OS globals (see YAGCD / Wii low memory map).  The disc's ID at 0 comes
    // with the game's files (port_mem_set_disc_id).
    const uintptr_t g = PORT_MEM1_BASE;
    storeBE32(g + 0x20, 0x0D15EA5E);           // boot magic
    storeBE32(g + 0x24, 1);                    // version
    storeBE32(g + 0x28, PORT_MEM1_SIZE);       // physical MEM1 size
    storeBE32(g + 0x2C, 0x10000006);           // console type: retail RVL
    storeBE32(g + 0xCC, 5);                    // TV mode: EURGB60
    storeBE32(g + 0xF0, PORT_MEM1_SIZE);       // simulated MEM1 size
    storeBE32(g + 0xF8, 243000000);            // bus clock
    storeBE32(g + 0xFC, 729000000);            // CPU clock
    storeBE32(g + 0x3118, PORT_MEM2_SIZE);     // physical MEM2 size
    storeBE32(g + 0x311C, PORT_MEM2_SIZE);     // simulated MEM2 size
    storeBE32(g + 0x3120, PORT_MEM2_BASE + PORT_MEM2_SIZE);
    storeBE32(g + 0x3124, PORT_MEM2_BASE + 0x800);
    storeBE32(g + 0x3128, PORT_MEM2_BASE + PORT_MEM2_SIZE);

    sArenaLo = (void*)(uintptr_t)(PORT_MEM1_BASE + 0x4000);
    sArenaHi = (void*)(uintptr_t)(PORT_MEM1_BASE + PORT_MEM1_SIZE);
    sMEM2ArenaLo = (void*)(uintptr_t)(PORT_MEM2_BASE + 0x800);
    sMEM2ArenaHi = (void*)(uintptr_t)(PORT_MEM2_BASE + PORT_MEM2_SIZE);
    storeBE32(g + 0x30, (u32)(uintptr_t)sArenaLo);
    storeBE32(g + 0x34, (u32)(uintptr_t)sArenaHi);

    sMemReady = true;
    PORT_LOG("mem: MEM1 %08x+%x MEM2 %08x+%x mapped (uncached mirrors at %08x/%08x)", PORT_MEM1_BASE, PORT_MEM1_SIZE, PORT_MEM2_BASE,
             PORT_MEM2_SIZE, PORT_MEM1_UNCACHED, PORT_MEM2_UNCACHED);
    return 1;
}

extern "C" void port_mem_set_disc_id(const char* id) {
    memcpy((void*)(uintptr_t)PORT_MEM1_BASE, id, 6);  // game code + maker
}

// ---------------------------------------------------------------------------
// Low pool: page-granular allocations below 4 GiB (thread stacks, etc).
// ---------------------------------------------------------------------------
static std::atomic<uintptr_t> sLowPoolNext{PORT_LOWPOOL_BASE};

extern "C" void* port_low_alloc(size_t size) {
    const size_t page = 0x4000;
    size = (size + page - 1) & ~(page - 1);
    uintptr_t addr = sLowPoolNext.fetch_add(size + page);  // leave an unmapped guard page after each block
    if (addr + size > PORT_LOWPOOL_BASE + PORT_LOWPOOL_SIZE) {
        port_fatal("low pool exhausted");
    }
    return mapAt(addr, size, -1);
}

extern "C" int port_is_game_memory(const void* p) {
    return guest_layout_is_valid_virtual((uintptr_t)p, 1);
}

extern "C" void* port_phys_to_host(uint32_t phys) {
    void* p = guest_layout_phys_to_virt(phys);
    if (!p) {
        return (void*)(uintptr_t)(0x80000000u + (phys & 0x3FFFFFFFu));
    }
    return p;
}

extern "C" int port_host_to_phys(const void* p, size_t size, uint32_t* outPhys) {
    int valid = 0;
    uint32_t phys = guest_layout_virt_range_to_phys(p, size, &valid);
    if (!valid) {
        port_log("port_host_to_phys: [%p, +0x%zx) is outside the game address space", p, size);
        return 0;
    }
    if (outPhys) {
        *outPhys = phys;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// OS arena API
// ---------------------------------------------------------------------------
extern "C" {

void* OSGetArenaLo(void) { return sArenaLo; }
void* OSGetArenaHi(void) { return sArenaHi; }
void OSSetArenaLo(void* p) { sArenaLo = p; }
void OSSetArenaHi(void* p) { sArenaHi = p; }
void* OSGetMEM1ArenaLo(void) { return sArenaLo; }
void* OSGetMEM1ArenaHi(void) { return sArenaHi; }
void OSSetMEM1ArenaLo(void* p) { sArenaLo = p; }
void OSSetMEM1ArenaHi(void* p) { sArenaHi = p; }
void* OSGetMEM2ArenaLo(void) { return sMEM2ArenaLo; }
void* OSGetMEM2ArenaHi(void) { return sMEM2ArenaHi; }
void OSSetMEM2ArenaLo(void* p) { sMEM2ArenaLo = p; }
void OSSetMEM2ArenaHi(void* p) { sMEM2ArenaHi = p; }

void* OSAllocFromArenaLo(u32 size, u32 align) {
    uintptr_t p = ((uintptr_t)sArenaLo + align - 1) & ~(uintptr_t)(align - 1);
    sArenaLo = (void*)(((p + size) + align - 1) & ~(uintptr_t)(align - 1));
    return (void*)p;
}

void* OSAllocFromArenaHi(u32 size, u32 align) {
    uintptr_t hi = ((uintptr_t)sArenaHi) & ~(uintptr_t)(align - 1);
    hi = (hi - size) & ~(uintptr_t)(align - 1);
    sArenaHi = (void*)hi;
    return (void*)hi;
}

void* OSAllocFromMEM1ArenaLo(u32 size, u32 align) { return OSAllocFromArenaLo(size, align); }
void* OSAllocFromMEM1ArenaHi(u32 size, u32 align) { return OSAllocFromArenaHi(size, align); }

// The game only calls OSInitAlloc to carve the OS heap descriptor array.
void* OSInitAlloc(void* arenaStart, void* arenaEnd, int maxHeaps) {
    (void)arenaEnd;
    uintptr_t p = (uintptr_t)arenaStart + (uintptr_t)maxHeaps * 12;
    return (void*)((p + 31) & ~(uintptr_t)31);
}

// Cache maintenance is meaningless on the host.
void DCFlushRange(void*, u32) {}
void DCFlushRangeNoSync(void*, u32) {}
void DCStoreRange(void*, u32) {}
void DCStoreRangeNoSync(void*, u32) {}
void DCInvalidateRange(void*, u32) {}
void DCZeroRange(void* addr, u32 nBytes) {
    uintptr_t a = (uintptr_t)addr & ~(uintptr_t)31;
    uintptr_t e = ((uintptr_t)addr + nBytes + 31) & ~(uintptr_t)31;
    memset((void*)a, 0, e - a);
}
void ICInvalidateRange(void*, u32) {}
void ICFlashInvalidate(void) {}
void LCEnable(void) {}
void LCDisable(void) {}
void OSProtectRange(u32, void*, u32, u32) {}

// Paired-single / HID register accessors.
u32 PPCMfhid2(void) { return 0; }
void PPCMthid2(u32) {}
u32 PPCMfmsr(void) { return 0x8000; }
void PPCMtmsr(u32) {}
u32 PPCMfwpar(void) { return 0; }
void PPCMtwpar(u32) {}
void PPCSync(void) {}
void PPCHalt(void) {
    port_fatal("PPCHalt");
}

}  // extern "C"
