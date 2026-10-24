#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/guest_layout.h"

static void testDefinitionsAndAlignment(void) {
    int valid = guest_layout_validate_definitions();
    assert(valid == 1);

    for (int i = 0; i < GUEST_REGION_COUNT; i++) {
        const GuestRegionDef* def = guest_layout_get_region((GuestRegionId)i);
        assert(def != NULL);
        assert((def->base & 0xFFFu) == 0);
        assert((def->size & 0xFFFu) == 0);
        assert(def->size > 0);
    }
    printf("[PASS] Guest memory definitions and 4KB alignments validated\n");
}

static void testOverlapAndOverflow(void) {
    assert(guest_layout_check_overlap(0x80000000u, 0x1000u, 0x80000800u, 0x1000u) == 1);
    assert(guest_layout_check_overlap(0x80000000u, 0x1000u, 0x80001000u, 0x1000u) == 0); // Abutting
    assert(guest_layout_check_overlap(0x80001000u, 0x1000u, 0x80000000u, 0x1000u) == 0);
    assert(guest_layout_check_overlap(0x80000000u, 0x0u, 0x80000000u, 0x1000u) == 0);
    // Overflow safety
    uintptr_t maxAddr = (uintptr_t)-1;
    assert(guest_layout_check_overlap(maxAddr - 0x100u, 0x200u, 0x1000u, 0x1000u) == 1);
    printf("[PASS] Overlap detection and arithmetic overflow safety verified\n");
}

static void testAddressTranslation(void) {
    // MEM1 cached & uncached
    void* v1 = guest_layout_phys_to_virt(0x00004000u);
    assert(v1 == (void*)(uintptr_t)0x80004000u);
    int valid = 0;
    uint32_t p1 = guest_layout_virt_to_phys((void*)(uintptr_t)0x80004000u, &valid);
    assert(valid == 1 && p1 == 0x00004000u);

    uint32_t p1u = guest_layout_virt_to_phys((void*)(uintptr_t)0xC0004000u, &valid);
    assert(valid == 1 && p1u == 0x00004000u);

    // MEM2 cached & uncached
    void* v2 = guest_layout_phys_to_virt(0x10000800u);
    assert(v2 == (void*)(uintptr_t)0x90000800u);
    uint32_t p2 = guest_layout_virt_to_phys((void*)(uintptr_t)0x90000800u, &valid);
    assert(valid == 1 && p2 == 0x10000800u);

    uint32_t p2u = guest_layout_virt_to_phys((void*)(uintptr_t)0xD0000800u, &valid);
    assert(valid == 1 && p2u == 0x10000800u);

    // LibGame & LowPool
    void* vlib = guest_layout_phys_to_virt(0x18000000u);
    assert(vlib == (void*)(uintptr_t)0x98000000u);
    uint32_t plib = guest_layout_virt_to_phys((void*)(uintptr_t)0x98000000u, &valid);
    assert(valid == 1 && plib == 0x18000000u);

    void* vpool = guest_layout_phys_to_virt(0x20000000u);
    assert(vpool == (void*)(uintptr_t)0xA0000000u);
    uint32_t ppool = guest_layout_virt_to_phys((void*)(uintptr_t)0xA0000000u, &valid);
    assert(valid == 1 && ppool == 0x20000000u);

    printf("[PASS] Bidirectional guest address translation verified\n");
}

static void testBoundsAndRejection(void) {
    int valid = 1;
    guest_layout_virt_to_phys(NULL, &valid);
    assert(valid == 0);

    guest_layout_virt_to_phys((void*)(uintptr_t)0x7FFFFFFFu, &valid);
    assert(valid == 0);

    guest_layout_virt_to_phys((void*)(uintptr_t)0x88000000u, &valid); // in gap
    assert(valid == 0);

    guest_layout_virt_to_phys((void*)(uintptr_t)0x7FFF00000000ull, &valid); // high 64-bit
    assert(valid == 0);

    assert(guest_layout_phys_to_virt(0x08000000u) == NULL); // MEM1 gap boundary
    assert(guest_layout_phys_to_virt(0x0FFFF000u) == NULL);
    assert(guest_layout_phys_to_virt(0x30000000u) == NULL); // lowpool end

    assert(guest_layout_is_valid_virtual(0x80000000u, 0x08000000u) == 1);
    assert(guest_layout_is_valid_virtual(0x80000000u, 0x08000001u) == 0); // overflow boundary
    assert(guest_layout_is_valid_virtual(0x88000000u, 0x1000u) == 0);     // gap
    assert(guest_layout_is_valid_virtual(0, 0x1000u) == 0);

    assert(guest_layout_is_valid_physical(0, 0x08000000u) == 1);
    assert(guest_layout_is_valid_physical(0, 0x08000001u) == 0);
    assert(guest_layout_is_valid_physical(0x08000000u, 0x1000u) == 0);

    printf("[PASS] Boundary rejection, invalid pointers, and out-of-range checks verified\n");
}

static void testCleanMapsAudit(void) {
    const char* cleanMaps =
        "00400000-00452000 r-xp 00000000 08:02 123456 /system/bin/app_process64\n"
        "7f80000000-7f90000000 rw-p 00000000 00:00 0 [anon:libc_malloc]\n"
        "7ffc000000-7ffd000000 rw-p 00000000 00:00 0 [stack]\n";

    GuestLayoutAudit audit;
    int res = guest_layout_audit_maps_buffer(cleanMaps, &audit);
    assert(res == 1);
    assert(audit.totalConflicts == 0);
    assert(audit.fullyAvailable == 1);
    for (int i = 0; i < GUEST_REGION_COUNT; i++) {
        assert(audit.regions[i].status == GUEST_RES_UNMAPPED);
    }
    printf("[PASS] Clean maps audit correctly reports zero conflicts\n");
}

static void testArtCollisionDetection(void) {
    const char* artCollisionMaps =
        "00400000-00452000 r-xp 00000000 08:02 123456 /system/bin/app_process64\n"
        "77000000-9f000000 rw-p 00000000 00:00 0 [anon:dalvik-large object space]\n"
        "7ffc000000-7ffd000000 rw-p 00000000 00:00 0 [stack]\n";

    GuestLayoutAudit audit;
    int res = guest_layout_audit_maps_buffer(artCollisionMaps, &audit);
    assert(res == 1);
    assert(audit.totalConflicts >= 3);
    assert(audit.fullyAvailable == 0);

    assert(audit.regions[GUEST_REGION_MEM1].status == GUEST_RES_CONFLICT);
    assert(strstr(audit.regions[GUEST_REGION_MEM1].conflictOwner, "dalvik-large object space") != NULL);

    assert(audit.regions[GUEST_REGION_MEM2].status == GUEST_RES_CONFLICT);
    assert(audit.regions[GUEST_REGION_LIB].status == GUEST_RES_CONFLICT);

    // Uncached mirrors and locked cache should remain unconflicted in this scenario
    assert(audit.regions[GUEST_REGION_MEM1_UNCACHED].status == GUEST_RES_UNMAPPED);
    assert(audit.regions[GUEST_REGION_LOCKED_CACHE].status == GUEST_RES_UNMAPPED);

    printf("[PASS] Zygote/ART Large Object Space collisions accurately detected\n");
}

static void testPreReservedWindowRecognition(void) {
    const char* preReservedMaps =
        "80000000-e0010000 ---p 00000000 00:00 0 [anon:petari game window]\n";

    GuestLayoutAudit audit;
    int res = guest_layout_audit_maps_buffer(preReservedMaps, &audit);
    assert(res == 1);
    assert(audit.totalConflicts == 0);
    assert(audit.fullyAvailable == 1);
    for (int i = 0; i < GUEST_REGION_COUNT; i++) {
        assert(audit.regions[i].status == GUEST_RES_PRE_RESERVED);
    }
    printf("[PASS] Pre-reserved PROT_NONE window properly recognized without false alarms\n");
}

static void testPartialGuardMappingIsConflict(void) {
    // An anonymous PROT_NONE guard that covers only half of MEM1 is not an own
    // reservation; treating it as pre-reserved would let MAP_FIXED clobber it.
    const char* partialGuard = "80000000-84000000 ---p 00000000 00:00 0\n";

    GuestLayoutAudit audit;
    guest_layout_audit_maps_buffer(partialGuard, &audit);
    assert(audit.totalConflicts >= 1);
    assert(audit.regions[GUEST_REGION_MEM1].status == GUEST_RES_CONFLICT);
    assert(audit.fullyAvailable == 0);
    printf("[PASS] Partial anonymous guard mapping is reported as a conflict\n");
}

static void testAuditReportFormatting(void) {
    const char* mixedMaps =
        "98000000-99000000 rwxp 00000000 00:00 0 [anon:jit-code-cache]\n";
    GuestLayoutAudit audit;
    guest_layout_audit_maps_buffer(mixedMaps, &audit);

    char report[2048];
    size_t len = guest_layout_format_audit_report(&audit, report, sizeof(report));
    assert(len > 0);
    assert(strstr(report, "MEM1_CACHED") != NULL);
    assert(strstr(report, "LIBGAME_SO") != NULL);
    assert(strstr(report, "jit-code-cache") != NULL);
    printf("[PASS] Audit report formatting verified\n");
}

static void testRangeTranslation(void) {
    int valid = 0;
    uint32_t phys = guest_layout_virt_range_to_phys((void*)(uintptr_t)0x80004000u, 0x1000u, &valid);
    assert(valid == 1 && phys == 0x00004000u);

    phys = guest_layout_virt_range_to_phys((void*)(uintptr_t)0xD0000000u, 0x08000000u, &valid);
    assert(valid == 1 && phys == 0x10000000u);

    // A range that ends one byte past its region must be rejected whole.
    guest_layout_virt_range_to_phys((void*)(uintptr_t)0x87FFF000u, 0x2000u, &valid);
    assert(valid == 0);
    guest_layout_virt_range_to_phys((void*)(uintptr_t)0x98000000u, 0x08000001u, &valid);
    assert(valid == 0);
    // A range straddling the cached/uncached gap is not one guest range.
    guest_layout_virt_range_to_phys((void*)(uintptr_t)0x87FFF000u, 0x40001000u, &valid);
    assert(valid == 0);
    // Wrap-around and empty ranges.
    guest_layout_virt_range_to_phys((void*)(uintptr_t)0xFFFFFFFFFFFFF000ull, 0x2000u, &valid);
    assert(valid == 0);
    guest_layout_virt_range_to_phys((void*)(uintptr_t)0x80004000u, 0, &valid);
    assert(valid == 0);
    guest_layout_virt_range_to_phys(NULL, 0x10u, &valid);
    assert(valid == 0);
    printf("[PASS] Whole-range guest address conversion rejects straddling and overflow\n");
}

int main(void) {
    printf("=== Running Guest Memory Subsystem Unit Tests ===\n");
    testDefinitionsAndAlignment();
    testOverlapAndOverflow();
    testAddressTranslation();
    testBoundsAndRejection();
    testRangeTranslation();
    testCleanMapsAudit();
    testArtCollisionDetection();
    testPreReservedWindowRecognition();
    testPartialGuardMappingIsConflict();
    testAuditReportFormatting();
    printf("All guest memory subsystem unit tests passed successfully.\n");
    return 0;
}
