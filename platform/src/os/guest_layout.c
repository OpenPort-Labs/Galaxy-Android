#include "port/guest_layout.h"

#include <stdio.h>
#include <string.h>

static const GuestRegionDef sRegions[GUEST_REGION_COUNT] = {
    { GUEST_REGION_MEM1,          "MEM1_CACHED",   0x80000000u, 0x08000000u, 0, 0 },
    { GUEST_REGION_MEM2,          "MEM2_CACHED",   0x90000000u, 0x08000000u, 0, 0 },
    { GUEST_REGION_LIB,           "LIBGAME_SO",    0x98000000u, 0x08000000u, 0, 0 },
    { GUEST_REGION_LOWPOOL,       "LOW_POOL",      0xA0000000u, 0x10000000u, 0, 0 },
    { GUEST_REGION_MEM1_UNCACHED, "MEM1_UNCACHED", 0xC0000000u, 0x08000000u, 1, 0x80000000u },
    { GUEST_REGION_MEM2_UNCACHED, "MEM2_UNCACHED", 0xD0000000u, 0x08000000u, 1, 0x90000000u },
    { GUEST_REGION_LOCKED_CACHE,  "LOCKED_CACHE",  0xE0000000u, 0x00010000u, 0, 0 },
};

const GuestRegionDef* guest_layout_get_region(GuestRegionId id) {
    if ((int)id < 0 || id >= GUEST_REGION_COUNT) return NULL;
    return &sRegions[id];
}

int guest_layout_check_overlap(uintptr_t baseA, size_t sizeA, uintptr_t baseB, size_t sizeB) {
    if (sizeA == 0 || sizeB == 0) return 0;
    if (baseA + sizeA < baseA || baseB + sizeB < baseB) return 1; // Overflow treated as collision
    uintptr_t endA = baseA + sizeA;
    uintptr_t endB = baseB + sizeB;
    return (baseA < endB && endA > baseB) ? 1 : 0;
}

int guest_layout_validate_definitions(void) {
    for (int i = 0; i < GUEST_REGION_COUNT; i++) {
        const GuestRegionDef* r = &sRegions[i];
        if (r->size == 0) return 0;
        if ((r->base & 0xFFFu) != 0 || (r->size & 0xFFFu) != 0) return 0; // Require 4KB page alignment
        if (r->base + r->size < r->base) return 0; // Overflow check
        for (int j = i + 1; j < GUEST_REGION_COUNT; j++) {
            if (guest_layout_check_overlap(r->base, r->size, sRegions[j].base, sRegions[j].size)) {
                return 0; // Region overlap forbidden
            }
        }
    }
    return 1;
}

int guest_layout_is_valid_virtual(uintptr_t addr, size_t size) {
    if (size == 0 || addr + size < addr) return 0;
    uintptr_t end = addr + size;
    for (int i = 0; i < GUEST_REGION_COUNT; i++) {
        const GuestRegionDef* r = &sRegions[i];
        if (addr >= r->base && end <= r->base + r->size) {
            return 1;
        }
    }
    return 0;
}

int guest_layout_is_valid_physical(uint32_t phys, size_t size) {
    if (size == 0 || (uint64_t)phys + size > 0x100000000ull) return 0;
    uint64_t end = (uint64_t)phys + size;
    if (end <= 0x08000000ull) return 1; // MEM1
    if (phys >= 0x10000000u && end <= 0x18000000ull) return 1; // MEM2
    if (phys >= 0x18000000u && end <= 0x20000000ull) return 1; // LibGame
    if (phys >= 0x20000000u && end <= 0x30000000ull) return 1; // LowPool
    return 0;
}

GuestRegionId guest_layout_identify_virtual(uintptr_t addr) {
    for (int i = 0; i < GUEST_REGION_COUNT; i++) {
        const GuestRegionDef* r = &sRegions[i];
        if (addr >= r->base && addr < r->base + r->size) {
            return (GuestRegionId)i;
        }
    }
    return GUEST_REGION_COUNT;
}

GuestRegionId guest_layout_identify_physical(uint32_t phys) {
    if (phys < 0x08000000u) return GUEST_REGION_MEM1;
    if (phys >= 0x10000000u && phys < 0x18000000u) return GUEST_REGION_MEM2;
    if (phys >= 0x18000000u && phys < 0x20000000u) return GUEST_REGION_LIB;
    if (phys >= 0x20000000u && phys < 0x30000000u) return GUEST_REGION_LOWPOOL;
    return GUEST_REGION_COUNT;
}

void* guest_layout_phys_to_virt(uint32_t phys) {
    if (phys < 0x08000000u) {
        return (void*)(uintptr_t)(0x80000000u + phys);
    }
    if (phys >= 0x10000000u && phys < 0x18000000u) {
        return (void*)(uintptr_t)(0x90000000u + (phys - 0x10000000u));
    }
    if (phys >= 0x18000000u && phys < 0x20000000u) {
        return (void*)(uintptr_t)(0x98000000u + (phys - 0x18000000u));
    }
    if (phys >= 0x20000000u && phys < 0x30000000u) {
        return (void*)(uintptr_t)(0xA0000000u + (phys - 0x20000000u));
    }
    return NULL;
}

uint32_t guest_layout_virt_to_phys(const void* p, int* outValid) {
    uintptr_t a = (uintptr_t)p;
    if (outValid) *outValid = 1;
    if (a >= 0x80000000u && a < 0x88000000u) {
        return (uint32_t)(a - 0x80000000u);
    }
    if (a >= 0x90000000u && a < 0x98000000u) {
        return (uint32_t)(0x10000000u + (a - 0x90000000u));
    }
    if (a >= 0x98000000u && a < 0xA0000000u) {
        return (uint32_t)(0x18000000u + (a - 0x98000000u));
    }
    if (a >= 0xA0000000u && a < 0xB0000000u) {
        return (uint32_t)(0x20000000u + (a - 0xA0000000u));
    }
    if (a >= 0xC0000000u && a < 0xC8000000u) {
        return (uint32_t)(a - 0xC0000000u);
    }
    if (a >= 0xD0000000u && a < 0xD8000000u) {
        return (uint32_t)(0x10000000u + (a - 0xD0000000u));
    }
    if (outValid) *outValid = 0;
    return 0;
}

size_t guest_layout_format_audit_report(const GuestLayoutAudit* audit, char* buf, size_t maxLen) {
    if (!audit || !buf || maxLen == 0) return 0;
    size_t off = 0;
    off += (size_t)snprintf(buf + off, (maxLen > off) ? maxLen - off : 0,
        "=== Guest Memory Layout Audit ===\nConflicts: %d, Available: %s\n",
        audit->totalConflicts, audit->fullyAvailable ? "YES" : "NO");

    for (int i = 0; i < GUEST_REGION_COUNT; i++) {
        const GuestRegionAudit* ra = &audit->regions[i];
        const char* st = "UNMAPPED";
        if (ra->status == GUEST_RES_PRE_RESERVED) st = "PRE_RESERVED";
        else if (ra->status == GUEST_RES_ACTIVE_MAPPED) st = "ACTIVE_MAPPED";
        else if (ra->status == GUEST_RES_CONFLICT) st = "CONFLICT";
        else if (ra->status == GUEST_RES_ERROR) st = "ERROR";

        off += (size_t)snprintf(buf + off, (maxLen > off) ? maxLen - off : 0,
            "  [%-14s] 0x%08lx-0x%08lx (%3zu MiB) : %-12s",
            ra->def.name, (unsigned long)ra->def.base,
            (unsigned long)(ra->def.base + ra->def.size),
            ra->def.size / (1024 * 1024), st);

        if (ra->status == GUEST_RES_CONFLICT) {
            off += (size_t)snprintf(buf + off, (maxLen > off) ? maxLen - off : 0,
                " (collides with: %s [0x%lx-0x%lx])",
                ra->conflictOwner[0] ? ra->conflictOwner : "<anon>",
                (unsigned long)ra->conflictLo, (unsigned long)ra->conflictHi);
        }
        off += (size_t)snprintf(buf + off, (maxLen > off) ? maxLen - off : 0, "\n");
    }
    return off;
}
