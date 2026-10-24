#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <cmath>
#include "gx/gx_builder.h"
#include "gx/vk/vk_canonical_state.h"

namespace gpu {

static inline VkBlendFactor vkBlendFactor(uint32_t raw) {
    return toVkBlendFactor(toCanonicalBlendFactor(raw));
}

static inline VkBlendOp vkBlendOp(uint32_t raw) {
    return toVkBlendOp(toCanonicalBlendOp(raw));
}

struct DrawBlendState {
    bool on = false;
    VkBlendFactor srcColor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstColor = VK_BLEND_FACTOR_ZERO;
    VkBlendFactor srcAlpha = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlpha = VK_BLEND_FACTOR_ZERO;
    VkBlendOp colorOp = VK_BLEND_OP_ADD;
    VkBlendOp alphaOp = VK_BLEND_OP_ADD;
    uint32_t constantAlpha = 0;
};

static inline DrawBlendState blendStateFor(const Item& it) {
    DrawBlendState b;
    b.on = (it.blend != 0) || (it.dstAlpha != 0);
    b.srcColor = it.blend ? vkBlendFactor(it.srcRGB) : VK_BLEND_FACTOR_ONE;
    b.dstColor = it.blend ? vkBlendFactor(it.dstRGB) : VK_BLEND_FACTOR_ZERO;
    b.srcAlpha = it.dstAlpha ? VK_BLEND_FACTOR_CONSTANT_ALPHA : vkBlendFactor(it.srcRGB);
    b.dstAlpha = it.dstAlpha ? VK_BLEND_FACTOR_ZERO : vkBlendFactor(it.dstRGB);
    b.colorOp = it.blend ? vkBlendOp(it.eq) : VK_BLEND_OP_ADD;
    b.alphaOp = it.dstAlpha ? VK_BLEND_OP_ADD : vkBlendOp(it.eq);
    b.constantAlpha = (uint32_t)lroundf(it.dstAlphaValue * 255.0f) & 0xFF;
    return b;
}

}  // namespace gpu
