#include "port/guest_layout.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__ANDROID__) || defined(__linux__)
#include <sys/mman.h>
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#endif

static uintptr_t sReservedBases[GUEST_REGION_COUNT];
static size_t sReservedSizes[GUEST_REGION_COUNT];
static int sReservedCount = 0;

// Drops the slices added from `first` onwards. Slices reserved by an earlier
// call stay tracked and mapped, so a repeated reserve followed by a release
// still frees everything this process mapped.
static void releaseFrom(int first) {
#if defined(__ANDROID__) || defined(__linux__)
    for (int i = first; i < sReservedCount; i++) {
        munmap((void*)sReservedBases[i], sReservedSizes[i]);
    }
#endif
    if (first < sReservedCount) sReservedCount = first;
}

static void parseMapLine(const char* line, GuestLayoutAudit* audit) {
    unsigned long long lo = 0, hi = 0;
    char perms[8] = {0};
    char path[256] = {0};
    int n = sscanf(line, "%llx-%llx %4s %*s %*s %*s %255[^\n]", &lo, &hi, perms, path);
    if (n < 3 || hi <= lo) return;

    for (int i = 0; i < GUEST_REGION_COUNT; i++) {
        GuestRegionAudit* ra = &audit->regions[i];
        if (guest_layout_check_overlap((uintptr_t)lo, (size_t)(hi - lo), ra->def.base, ra->def.size)) {
            int isProtNone = (strncmp(perms, "---p", 4) == 0);
            // Only a PROT_NONE mapping that covers the whole region can be an
            // own reservation. A partial anonymous guard mapping belongs to
            // someone else and must be reported as a conflict, not skipped and
            // later overwritten by MAP_FIXED.
            int coversRegion = (uintptr_t)lo <= ra->def.base &&
                               (uintptr_t)hi >= ra->def.base + ra->def.size;
            int isOwnReservation = isProtNone && coversRegion &&
                                   (strstr(path, "petari") != NULL ||
                                    strstr(path, "guest") != NULL ||
                                    path[0] == '\0');
            if (isOwnReservation) {
                if (ra->status == GUEST_RES_UNMAPPED) {
                    ra->status = GUEST_RES_PRE_RESERVED;
                }
            } else {
                ra->status = GUEST_RES_CONFLICT;
                audit->totalConflicts++;
                ra->conflictLo = (uintptr_t)lo;
                ra->conflictHi = (uintptr_t)hi;
                snprintf(ra->conflictOwner, sizeof(ra->conflictOwner), "%s [%s]",
                         path[0] ? path : "<anon>", perms);
            }
        }
    }
}

static void initAudit(GuestLayoutAudit* audit) {
    memset(audit, 0, sizeof(*audit));
    for (int i = 0; i < GUEST_REGION_COUNT; i++) {
        const GuestRegionDef* def = guest_layout_get_region((GuestRegionId)i);
        if (def) audit->regions[i].def = *def;
        audit->regions[i].status = GUEST_RES_UNMAPPED;
    }
    audit->fullyAvailable = 1;
}

int guest_layout_audit_maps_stream(FILE* f, GuestLayoutAudit* outAudit) {
    if (!outAudit) return 0;
    initAudit(outAudit);
    if (!f) return 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        parseMapLine(line, outAudit);
    }
    outAudit->fullyAvailable = (outAudit->totalConflicts == 0);
    return 1;
}

int guest_layout_audit_maps_buffer(const char* mapsText, GuestLayoutAudit* outAudit) {
    if (!outAudit) return 0;
    initAudit(outAudit);
    if (!mapsText) return 0;

    const char* cur = mapsText;
    char line[512];
    while (*cur) {
        const char* next = strchr(cur, '\n');
        size_t len = next ? (size_t)(next - cur) : strlen(cur);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, cur, len);
        line[len] = '\0';
        parseMapLine(line, outAudit);
        if (!next) break;
        cur = next + 1;
    }
    outAudit->fullyAvailable = (outAudit->totalConflicts == 0);
    return 1;
}

int guest_layout_reserve_slices_nondestructive(GuestLayoutAudit* outAudit) {
    GuestLayoutAudit audit;
    FILE* f = fopen("/proc/self/maps", "r");
    if (f) {
        guest_layout_audit_maps_stream(f, &audit);
        fclose(f);
    } else {
        initAudit(&audit);
    }

    if (audit.totalConflicts > 0) {
        if (outAudit) *outAudit = audit;
        return 0; // Collisions present; strictly avoid clobbering with MAP_FIXED
    }

#if defined(__ANDROID__) || defined(__linux__)
    int firstNew = sReservedCount;
    for (int i = 0; i < GUEST_REGION_COUNT; i++) {
        GuestRegionAudit* ra = &audit.regions[i];
        if (ra->status == GUEST_RES_PRE_RESERVED) continue; // Already safely reserved

        if (sReservedCount >= GUEST_REGION_COUNT) {
            ra->status = GUEST_RES_CONFLICT;
            audit.totalConflicts++;
            audit.fullyAvailable = 0;
            snprintf(ra->conflictOwner, sizeof(ra->conflictOwner), "reservation table exhausted");
            releaseFrom(firstNew);
            if (outAudit) *outAudit = audit;
            return 0;
        }

        void* p = mmap((void*)ra->def.base, ra->def.size, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
        if (p == MAP_FAILED || p != (void*)ra->def.base) {
            if (p != MAP_FAILED) munmap(p, ra->def.size);
            ra->status = GUEST_RES_CONFLICT;
            audit.totalConflicts++;
            audit.fullyAvailable = 0;
            snprintf(ra->conflictOwner, sizeof(ra->conflictOwner), "mmap EEXIST / collision");
            releaseFrom(firstNew); // Roll back only the slices mapped by this call
            if (outAudit) *outAudit = audit;
            return 0;
        }
        sReservedBases[sReservedCount] = ra->def.base;
        sReservedSizes[sReservedCount] = ra->def.size;
        sReservedCount++;
        ra->status = GUEST_RES_ACTIVE_MAPPED;
    }
#endif

    audit.fullyAvailable = 1;
    if (outAudit) *outAudit = audit;
    return 1;
}

void guest_layout_release_slices(void) {
    releaseFrom(0);
}
