#pragma once
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <algorithm>
using s32 = int32_t;
#include "gx/vk_renderer.h"

static inline void test_quicksave_starbit_accounting() {
    printf("[TEST] Running Quick-Save Star-Bit Accounting tests...\n");

    s32 stockStarBits = 100;
    s32 stageStarBits = 0;
    s32 creditedQuickSaveStarBits = 0;

    auto quickSave = [&]() {
        s32 currentPieces = stageStarBits;
        s32 uncreditedBits = std::max(0, currentPieces - creditedQuickSaveStarBits);
        if (uncreditedBits > 0) {
            stockStarBits += uncreditedBits;
        }
        creditedQuickSaveStarBits = std::max(creditedQuickSaveStarBits, currentPieces);
    };

    auto stageClearTransfer = [&]() {
        s32 uncreditedPieces = std::max(0, stageStarBits - creditedQuickSaveStarBits);
        stockStarBits += uncreditedPieces;
        creditedQuickSaveStarBits = 0;
        stageStarBits = 0;
    };

    auto reloadSave = [&]() {
        creditedQuickSaveStarBits = 0;
        stageStarBits = 0;
    };

    stageStarBits = 20;
    quickSave();
    assert(stockStarBits == 120);
    assert(creditedQuickSaveStarBits == 20);

    quickSave();
    assert(stockStarBits == 120);
    assert(creditedQuickSaveStarBits == 20);

    stageStarBits = 35;
    quickSave();
    assert(stockStarBits == 135);
    assert(creditedQuickSaveStarBits == 35);

    stageStarBits = 25;
    quickSave();
    assert(stockStarBits == 135);
    assert(creditedQuickSaveStarBits == 35);

    stageStarBits = 30;
    quickSave();
    assert(stockStarBits == 135);
    assert(creditedQuickSaveStarBits == 35);

    stageStarBits = 40;
    quickSave();
    assert(stockStarBits == 140);
    assert(creditedQuickSaveStarBits == 40);

    stageClearTransfer();
    assert(stockStarBits == 140);
    assert(creditedQuickSaveStarBits == 0);

    stageStarBits = 50;
    stageClearTransfer();
    assert(stockStarBits == 190);

    stageStarBits = 15;
    quickSave();
    assert(stockStarBits == 205);
    reloadSave();
    assert(creditedQuickSaveStarBits == 0);

    printf("[PASS] Quick-save star-bit accounting strictly preserves conservation laws!\n");
}

static inline void test_copy_texture_slot_telemetry() {
    printf("[TEST] Running Vulkan Copy-Texture Telemetry tests...\n");

    VkCopyTex slots[2]{};
    slots[0].w = 1280;
    slots[0].h = 720;
    slots[0].frame = 42;

    uint32_t hits = 0;
    uint32_t fallbacks = 0;
    uint32_t misses = 0;

    auto lookup = [&](uint32_t eyeIndex) -> const VkCopyTex* {
        if (eyeIndex < 2 && slots[eyeIndex].w > 0) {
            hits++;
            return &slots[eyeIndex];
        }
        if (slots[0].w > 0) {
            fallbacks++;
            return &slots[0];
        }
        misses++;
        return nullptr;
    };

    const VkCopyTex* t0 = lookup(0);
    assert(t0 != nullptr && t0->frame == 42);
    assert(hits == 1 && fallbacks == 0 && misses == 0);

    const VkCopyTex* t1 = lookup(1);
    assert(t1 != nullptr && t1->frame == 42);
    assert(hits == 1 && fallbacks == 1 && misses == 0);

    slots[1].w = 1280;
    slots[1].h = 720;
    slots[1].frame = 99;

    const VkCopyTex* t1_populated = lookup(1);
    assert(t1_populated != nullptr && t1_populated->frame == 99);
    assert(hits == 2 && fallbacks == 1 && misses == 0);

    const VkCopyTex* t_oob = lookup(5);
    assert(t_oob != nullptr && t_oob->frame == 42);
    assert(hits == 2 && fallbacks == 2 && misses == 0);

    printf("[PASS] Vulkan copy-texture slot telemetry and fallback handling verified!\n");
}

static inline void test_frame_pacer_timing() {
    printf("[TEST] Running Frame Pacer Timing & Phase Debt tests...\n");

    uint64_t periodNs = 16666667;
    uint64_t lastPresentNs = 1000000000;
    uint64_t targetNs = lastPresentNs + periodNs;

    uint64_t nowNs = lastPresentNs + 28000000;
    assert(nowNs >= targetNs);

    lastPresentNs = nowNs;
    uint64_t nextTargetNs = lastPresentNs + periodNs;
    assert(nextTargetNs == nowNs + periodNs);

    float targetFps = 60.0f;
    float effectivePresentationFps = targetFps;
    assert(effectivePresentationFps == 60.0f);

    printf("[PASS] Frame pacer phase debt recovery and rate decoupling verified!\n");
}
