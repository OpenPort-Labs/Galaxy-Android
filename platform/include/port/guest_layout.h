#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Defined guest regions required by the Super Mario Galaxy / Wii architecture.
typedef enum GuestRegionId {
    GUEST_REGION_MEM1 = 0,         // 0x80000000, 128 MiB (cached MEM1)
    GUEST_REGION_MEM2,             // 0x90000000, 128 MiB (cached MEM2)
    GUEST_REGION_LIB,              // 0x98000000, 128 MiB (libgame.so image)
    GUEST_REGION_LOWPOOL,          // 0xA0000000, 256 MiB (stacks & low arena)
    GUEST_REGION_MEM1_UNCACHED,    // 0xC0000000, 128 MiB (uncached mirror MEM1)
    GUEST_REGION_MEM2_UNCACHED,    // 0xD0000000, 128 MiB (uncached mirror MEM2)
    GUEST_REGION_LOCKED_CACHE,     // 0xE0000000, 64 KiB (scratchpad)
    GUEST_REGION_COUNT
} GuestRegionId;

typedef struct GuestRegionDef {
    GuestRegionId id;
    const char* name;
    uintptr_t base;
    size_t size;
    int isMirrored;
    uintptr_t mirrorOf;
} GuestRegionDef;

typedef enum GuestReservationStatus {
    GUEST_RES_UNMAPPED = 0,
    GUEST_RES_PRE_RESERVED,
    GUEST_RES_ACTIVE_MAPPED,
    GUEST_RES_CONFLICT,
    GUEST_RES_ERROR
} GuestReservationStatus;

typedef struct GuestRegionAudit {
    GuestRegionDef def;
    GuestReservationStatus status;
    char conflictOwner[128];
    uintptr_t conflictLo;
    uintptr_t conflictHi;
} GuestRegionAudit;

typedef struct GuestLayoutAudit {
    GuestRegionAudit regions[GUEST_REGION_COUNT];
    int totalConflicts;
    int fullyAvailable;
} GuestLayoutAudit;

// Returns definition of a region by ID.
const GuestRegionDef* guest_layout_get_region(GuestRegionId id);

// Validates that all defined regions are page-aligned (>= 4KB) and do not overlap.
// Returns 1 if valid, 0 if invalid.
int guest_layout_validate_definitions(void);

// Checks if range [baseA, baseA + sizeA) overlaps [baseB, baseB + sizeB).
// Returns 1 if overlapping, 0 if disjoint.
int guest_layout_check_overlap(uintptr_t baseA, size_t sizeA, uintptr_t baseB, size_t sizeB);

// Safe address translation & range checks
int guest_layout_is_valid_virtual(uintptr_t addr, size_t size);
int guest_layout_is_valid_physical(uint32_t phys, size_t size);
GuestRegionId guest_layout_identify_virtual(uintptr_t addr);
GuestRegionId guest_layout_identify_physical(uint32_t phys);
void* guest_layout_phys_to_virt(uint32_t phys);
uint32_t guest_layout_virt_to_phys(const void* p, int* outValid);

// Audit memory map from a /proc/self/maps file stream or string buffer.
int guest_layout_audit_maps_stream(FILE* f, GuestLayoutAudit* outAudit);
int guest_layout_audit_maps_buffer(const char* mapsText, GuestLayoutAudit* outAudit);

// Non-destructive reservation using MAP_FIXED_NOREPLACE (or equivalent probe).
// Returns 1 if all required slices are safely reserved without clobbering, 0 on conflict.
// If any slice collides, all slices reserved during this call are rolled back.
int guest_layout_reserve_slices_nondestructive(GuestLayoutAudit* outAudit);

// Releases slices reserved by guest_layout_reserve_slices_nondestructive.
void guest_layout_release_slices(void);

// Formats diagnostic report into a string buffer.
size_t guest_layout_format_audit_report(const GuestLayoutAudit* audit, char* buf, size_t maxLen);

#ifdef __cplusplus
}
#endif
