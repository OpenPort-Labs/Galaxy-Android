#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <cmath>
#include "gx/gx_builder.h"
#include "gx/vk/vk_canonical_state.h"

namespace gpu {

static inline VkBlendFactor vkBlendFactor(uint32_t glFactor) {
    switch (glFactor) {
    case 0: return VK_BLEND_FACTOR_ZERO;
    case 1: return VK_BLEND_FACTOR_ONE;
    case 0x0300: return VK_BLEND_FACTOR_SRC_COLOR;
    case 0x0301: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 0x0302: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 0x0303: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 0x0304: return VK_BLEND_FACTOR_DST_ALPHA;
    case 0x0305: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 0x0306: return VK_BLEND_FACTOR_DST_COLOR;
    case 0x0307: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 0x8001: return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case 0x8002: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case 0x8003: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case 0x8004: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    default: return VK_BLEND_FACTOR_ONE;
    }
}

static inline VkBlendOp vkBlendOp(uint32_t glEq) {
    switch (glEq) {
    case 0x8007: return VK_BLEND_OP_SUBTRACT;
    case 0x8008: return VK_BLEND_OP_REVERSE_SUBTRACT;
    default: return VK_BLEND_OP_ADD;
    }
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
