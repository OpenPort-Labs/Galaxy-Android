#pragma once
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "gx/vk/vk_hot_path.h"

using namespace gpu;

namespace {

inline ShaderUid uid_with_vtx_flags(uint32_t flags) {
    ShaderUid uid{};
    uid.vtxFlags = flags;
    return uid;
}

inline PipelineKey key_for(uint32_t flags, bool depthWrite, uint32_t blendDst,
                           CanonicalCullMode cull, uint32_t colorMask) {
    DecodedPipelineKey d{};
    d.uid = uid_with_vtx_flags(flags);
    d.vtxFlags = flags;
    d.depthTest = true;
    d.depthWrite = depthWrite;
    d.depthCompareOp = CanonicalDepthFunc::LessOrEqual;
    d.blendEnable = true;
    d.srcColorBlendFactor = CanonicalBlendFactor::SrcAlpha;
    d.dstColorBlendFactor = (CanonicalBlendFactor)blendDst;
    d.srcAlphaBlendFactor = CanonicalBlendFactor::One;
    d.dstAlphaBlendFactor = CanonicalBlendFactor::Zero;
    d.colorBlendOp = CanonicalBlendOp::Add;
    d.alphaBlendOp = CanonicalBlendOp::Add;
    d.colorWriteMask = colorMask;
    d.blendConstantAlpha = 0.5f;
    d.cullMode = cull;
    d.topology = CanonicalPrimitiveTopology::TriangleList;
    return encodePipelineKey(d);
}

inline VkDescriptorSet fake_set(uintptr_t value) {
    return (VkDescriptorSet)value;
}

inline VkPipeline fake_pipeline(uintptr_t value) {
    return (VkPipeline)value;
}

}  // namespace

// Exercises the production PipelineMru that VkRenderer::getPipeline uses.
static inline void test_pipeline_mru_production() {
    printf("[TEST] Running production PipelineMru cache tests...\n");

    const PipelineKey keyA = key_for(0x4047, true, (uint32_t)CanonicalBlendFactor::OneMinusSrcAlpha,
                                     CanonicalCullMode::Back, 0x0F);
    const PipelineKey keyB = key_for(0x4047, false, (uint32_t)CanonicalBlendFactor::One,
                                     CanonicalCullMode::None, 0x07);

    PipelineMru mru;
    assert(mru.empty());
    assert(mru.lookup(keyA) == VK_NULL_HANDLE);

    mru.store(keyA, fake_pipeline(0x1000));
    assert(!mru.empty());
    assert(mru.lookup(keyA) == fake_pipeline(0x1000));

    // A different key must never return the cached pipeline.
    assert(mru.lookup(keyB) == VK_NULL_HANDLE);

    // Alternating keys: switching back only hits after the entry is stored again.
    mru.store(keyB, fake_pipeline(0x2000));
    assert(mru.lookup(keyB) == fake_pipeline(0x2000));
    assert(mru.lookup(keyA) == VK_NULL_HANDLE);
    mru.store(keyA, fake_pipeline(0x1000));
    assert(mru.lookup(keyA) == fake_pipeline(0x1000));

    // The cache stores the key by value, not by reference.
    PipelineKey keyCopy = keyA;
    mru.store(keyCopy, fake_pipeline(0x3000));
    keyCopy.flags ^= 1u;
    assert(mru.lookup(keyA) == fake_pipeline(0x3000));

    // reset() must make a previously cached key miss; this is the shutdown and
    // swapchain-rebuild invalidation path that used to leak destroyed handles.
    mru.reset();
    assert(mru.empty());
    assert(mru.lookup(keyA) == VK_NULL_HANDLE);

    // A null pipeline must never be reported as a cache hit.
    mru.store(keyA, VK_NULL_HANDLE);
    assert(mru.lookup(keyA) == VK_NULL_HANDLE);
    assert(mru.empty());

    // Every encoded state component must participate in the comparison.
    const PipelineKey base = key_for(0x4047, true, (uint32_t)CanonicalBlendFactor::OneMinusSrcAlpha,
                                     CanonicalCullMode::Back, 0x0F);
    PipelineKey variants[6];
    variants[0] = base;
    variants[0].flags ^= 1u;
    variants[1] = base;
    variants[1].depthState ^= (1u << 1);  // depth write
    variants[2] = base;
    variants[2].blendState ^= (1u << 6);  // destination colour factor
    variants[3] = base;
    variants[3].blendState2 ^= (1u << 6);  // colour write mask
    variants[4] = base;
    variants[4].rasterState ^= (1u << 2);  // topology
    variants[5] = base;
    variants[5].uid.colorCtrl[0] ^= 1u;     // shader identity

    mru.store(base, fake_pipeline(0x4000));
    for (const PipelineKey& v : variants) {
        assert(!(v == base));
        assert(mru.lookup(v) == VK_NULL_HANDLE);
        assert(PipelineKeyHash()(v) != PipelineKeyHash()(base));
    }
    assert(mru.lookup(base) == fake_pipeline(0x4000));

    printf("[PASS] PipelineMru invalidates on reset and keys on every state component!\n");
}

// Exercises the production texture-unit walk used by the draw dispatch.
static inline void test_tex_mask_iteration_production() {
    printf("[TEST] Running production texture-mask iteration tests...\n");

    for (uint32_t mask = 0; mask <= 0xFFu; mask++) {
        uint32_t visited[kTexUnitCount + 1];
        uint32_t count = 0;
        forEachActiveTexUnit(mask, [&](uint32_t unit) {
            assert(unit < kTexUnitCount);
            assert(count < kTexUnitCount);
            visited[count++] = unit;
        });

        uint32_t expectedCount = 0;
        for (uint32_t unit = 0; unit < kTexUnitCount; unit++) {
            if (mask & (1u << unit)) expectedCount++;
        }
        assert(count == expectedCount);

        // Reference: ascending order, only set bits.
        uint32_t seen = 0;
        for (uint32_t unit = 0; unit < kTexUnitCount; unit++) {
            if (!(mask & (1u << unit))) continue;
            assert(seen < count);
            assert(visited[seen] == unit);
            seen++;
        }
        assert(seen == count);
    }

    // Sparse masks called out by the audit brief.
    struct SparseCase {
        uint32_t mask;
        uint32_t units[8];
        uint32_t count;
    } sparse[] = {
        {0x00u, {0}, 0},
        {0x01u, {0}, 1},
        {0x05u, {0, 2}, 2},           // 0b00000101
        {0x81u, {0, 7}, 2},           // 0b10000001
        {0xA5u, {0, 2, 5, 7}, 4},     // 0b10100101
        {0xFFu, {0, 1, 2, 3, 4, 5, 6, 7}, 8},
    };
    for (const SparseCase& c : sparse) {
        uint32_t seen = 0;
        forEachActiveTexUnit(c.mask, [&](uint32_t unit) {
            assert(seen < c.count);
            assert(unit == c.units[seen]);
            seen++;
        });
        assert(seen == c.count);
    }

    // Bits above the eighth unit are not texture units and must be ignored.
    uint32_t extra = 0;
    forEachActiveTexUnit(0xFFFFFF00u, [&](uint32_t) { extra++; });
    assert(extra == 0);
    extra = 0;
    forEachActiveTexUnit(0xFFFFFFFFu, [&](uint32_t unit) {
        assert(unit == extra);
        extra++;
    });
    assert(extra == kTexUnitCount);

    for (uint32_t unit = 0; unit < kTexUnitCount; unit++) {
        assert(lowestSetBit(1u << unit) == unit);
    }
    assert(activeTexUnitMask(0xFFFFFFFFu) == 0xFFu);

    printf("[PASS] Texture-mask walk matches the reference for all 256 masks!\n");
}

// Exercises the production descriptor-set cache decision.
static inline void test_draw_descriptor_cache_production() {
    printf("[TEST] Running production draw descriptor-cache tests...\n");

    DrawTexState textured;
    textured.mask = 0x05u;
    textured.tex[0] = 7;
    textured.tex[2] = 9;
    textured.mode0[0] = 0x00000022;
    textured.mode1[2] = 0x00000011;

    DrawDescriptorCache cache;
    assert(cache.lookup(textured, 1) == VK_NULL_HANDLE);

    cache.store(textured, fake_set(0x9000), 1);
    assert(cache.lookup(textured, 1) == fake_set(0x9000));

    // Any change in the descriptor identity forces a fresh resolution.
    DrawTexState changed = textured;
    changed.tex[2] = 10;
    assert(cache.lookup(changed, 1) == VK_NULL_HANDLE);

    changed = textured;
    changed.mode0[0] ^= 0x20u;  // minification filter bits
    assert(cache.lookup(changed, 1) == VK_NULL_HANDLE);

    changed = textured;
    changed.mode1[2] ^= 0x01u;  // LOD bias bits
    assert(cache.lookup(changed, 1) == VK_NULL_HANDLE);

    changed = textured;
    changed.mask = 0x07u;
    assert(cache.lookup(changed, 1) == VK_NULL_HANDLE);

    // A descriptor-pool reset / view invalidation bumps the epoch.
    assert(cache.lookup(textured, 2) == VK_NULL_HANDLE);

    // Untextured draws compare on the empty mask alone.
    DrawTexState untexturedA;
    DrawTexState untexturedB;
    untexturedA.tex[0] = ~0u;
    untexturedB.tex[3] = 0xDEADBEEF;
    cache.store(untexturedA, fake_set(0xB000), 3);
    assert(cache.lookup(untexturedB, 3) == fake_set(0xB000));

    cache.reset();
    assert(cache.lookup(untexturedB, 3) == VK_NULL_HANDLE);

    printf("[PASS] Draw descriptor cache keys on texture identity and epoch!\n");
}
