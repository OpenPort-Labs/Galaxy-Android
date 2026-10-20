// Hot-path helpers shared by the Vulkan draw dispatch, the pipeline cache and
// their regression tests.  They live here (instead of inline in the renderer)
// so the tests compile and run the same code the renderer executes rather than
// a model of it.
#pragma once

#include <cstdint>
#include <cstring>

#include <vulkan/vulkan.h>

#include "gx/vk/vk_pipeline_key.h"

namespace gpu {

// GX exposes eight texture units; recordings never set bits above them.
constexpr uint32_t kTexUnitCount = 8;
constexpr uint32_t kTexValidMask = (1u << kTexUnitCount) - 1u;

inline uint32_t activeTexUnitMask(uint32_t recordedMask) {
    return recordedMask & kTexValidMask;
}

// Index of the lowest set bit.  Precondition: value != 0.
inline uint32_t lowestSetBit(uint32_t value) {
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__builtin_ctz(value);
#else
    uint32_t index = 0;
    while ((value & 1u) == 0) {
        value >>= 1;
        ++index;
    }
    return index;
#endif
}

// Visits the active texture units of `mask` from unit 0 upwards, skipping
// inactive units without testing each one.  `fn` receives the unit index.
template <typename F>
inline void forEachActiveTexUnit(uint32_t mask, F&& fn) {
    uint32_t active = activeTexUnitMask(mask);
    while (active) {
        fn(lowestSetBit(active));
        active &= active - 1u;
    }
}

// Texture state of the last draw whose descriptor set was resolved.  It is the
// descriptor identity of that draw, so it must include every input that decides
// which image views and samplers the set is built from.
struct DrawTexState {
    uint32_t mask = 0;
    uint32_t tex[kTexUnitCount] = {};
    uint32_t mode0[kTexUnitCount] = {};
    uint32_t mode1[kTexUnitCount] = {};
};

// True when `cur` resolves to the same descriptor set as `prev`.  Untextured
// draws compare on the empty mask alone: every slot is the dummy view.
inline bool sameDrawTexState(const DrawTexState& prev, const DrawTexState& cur) {
    if (prev.mask != cur.mask) return false;
    if (cur.mask == 0) return true;
    return std::memcmp(prev.tex, cur.tex, sizeof(cur.tex)) == 0 &&
           std::memcmp(prev.mode0, cur.mode0, sizeof(cur.mode0)) == 0 &&
           std::memcmp(prev.mode1, cur.mode1, sizeof(cur.mode1)) == 0;
}

// Descriptor-set cache for the draw loop: the set resolved for the previous
// draw is reused only while its descriptor identity and the descriptor epoch
// (bumped whenever cached sets are freed) are unchanged.
class DrawDescriptorCache {
public:
    // Cached set for `cur`, or VK_NULL_HANDLE when the draw must resolve again.
    VkDescriptorSet lookup(const DrawTexState& cur, uint64_t descEpoch) const {
        if (!mValid || mSet == VK_NULL_HANDLE || mEpoch != descEpoch) return VK_NULL_HANDLE;
        return sameDrawTexState(mState, cur) ? mSet : VK_NULL_HANDLE;
    }

    void store(const DrawTexState& cur, VkDescriptorSet set, uint64_t descEpoch) {
        mState = cur;
        mSet = set;
        mEpoch = descEpoch;
        mValid = true;
    }

    void reset() {
        mState = DrawTexState{};
        mSet = VK_NULL_HANDLE;
        mEpoch = 0;
        mValid = false;
    }

private:
    DrawTexState mState;
    VkDescriptorSet mSet = VK_NULL_HANDLE;
    uint64_t mEpoch = 0;
    bool mValid = false;
};

// One-entry most-recently-used cache in front of VkRenderer::mPipelines.
class PipelineMru {
public:
    // Cached pipeline for `key`, or VK_NULL_HANDLE when the cache misses.
    VkPipeline lookup(const PipelineKey& key) const {
        return (mPipeline != VK_NULL_HANDLE && key == mKey) ? mPipeline : VK_NULL_HANDLE;
    }

    void store(const PipelineKey& key, VkPipeline pipeline) {
        mKey = key;
        mPipeline = pipeline;
    }

    // Must run whenever the pipeline map is cleared/refilled or the device is
    // replaced: a destroyed pipeline handle must never be handed out again.
    void reset() {
        mPipeline = VK_NULL_HANDLE;
        mKey = PipelineKey{};
    }

    bool empty() const { return mPipeline == VK_NULL_HANDLE; }

private:
    PipelineKey mKey{};
    VkPipeline mPipeline = VK_NULL_HANDLE;
};

}  // namespace gpu
