#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cstddef>
#include "gx/gx_record.h"
#include "gx/vk_shadergen.h"

namespace gpu {

struct VkTarget {
    int width = 0;
    int height = 0;
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkImageLayout colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool hasValidContent = false;
};

struct VkCopyTex {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    int w = 0;
    int h = 0;
    uint64_t frame = 0;
};

struct PipelineKey {
    ShaderUid uid;
    uint32_t flags;
    uint32_t depthState;   // test(1) | (write << 1) | (depthFunc << 4)
    uint32_t blendState;   // blendOn(1) | (srcRGB << 1) | (dstRGB << 6) | (srcAlpha << 11) | (dstAlpha << 16)
    uint32_t blendState2;  // colorOp(2) | (alphaOp << 2) | (colorMask << 4) | (constantAlpha << 8)
    uint32_t rasterState;  // cull(1) | (cullFace << 1) | (prim << 5) | (frontFace << 8)

    bool operator==(const PipelineKey& o) const {
        return uid == o.uid && flags == o.flags && depthState == o.depthState &&
               blendState == o.blendState && blendState2 == o.blendState2 && rasterState == o.rasterState;
    }
};

struct PipelineKeyHash {
    size_t operator()(const PipelineKey& k) const {
        size_t h = ShaderUidHash()(k.uid);
        h = (h ^ k.flags) * 1099511628211ull;
        h = (h ^ k.depthState) * 1099511628211ull;
        h = (h ^ k.blendState) * 1099511628211ull;
        h = (h ^ k.blendState2) * 1099511628211ull;
        h = (h ^ k.rasterState) * 1099511628211ull;
        return h;
    }
};

// Canonical decoded state representation
struct DecodedPipelineKey {
    ShaderUid uid{};
    uint32_t vtxFlags = 0;

    // Depth state
    bool depthTest = false;
    bool depthWrite = false;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_ALWAYS;

    // Blend state
    bool blendEnable = false;
    VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
    VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alphaBlendOp = VK_BLEND_OP_ADD;
    VkColorComponentFlags colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    float blendConstantAlpha = 0.0f;

    // Raster state
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
};

inline PipelineKey encodePipelineKey(const DecodedPipelineKey& d) {
    PipelineKey k{};
    k.uid = d.uid;
    k.flags = d.vtxFlags;
    k.depthState = (d.depthTest ? 1u : 0u) |
                   ((d.depthTest && d.depthWrite ? 1u : 0u) << 1) |
                   (((uint32_t)d.depthCompareOp & 7u) << 4);

    k.blendState = (d.blendEnable ? 1u : 0u) |
                   (((uint32_t)d.srcColorBlendFactor & 0x1Fu) << 1) |
                   (((uint32_t)d.dstColorBlendFactor & 0x1Fu) << 6) |
                   (((uint32_t)d.srcAlphaBlendFactor & 0x1Fu) << 11) |
                   (((uint32_t)d.dstAlphaBlendFactor & 0x1Fu) << 16);

    uint32_t maskBits = 0;
    if (d.colorWriteMask & VK_COLOR_COMPONENT_R_BIT) maskBits |= 1;
    if (d.colorWriteMask & VK_COLOR_COMPONENT_G_BIT) maskBits |= 2;
    if (d.colorWriteMask & VK_COLOR_COMPONENT_B_BIT) maskBits |= 4;
    if (d.colorWriteMask & VK_COLOR_COMPONENT_A_BIT) maskBits |= 8;
    float clampedAlpha = (d.blendConstantAlpha < 0.0f) ? 0.0f : ((d.blendConstantAlpha > 1.0f) ? 1.0f : d.blendConstantAlpha);
    uint32_t constAlpha = (uint32_t)(clampedAlpha * 255.0f + 0.5f) & 0xFF;

    k.blendState2 = ((uint32_t)d.colorBlendOp & 3u) |
                    (((uint32_t)d.alphaBlendOp & 3u) << 2) |
                    ((maskBits & 0x0Fu) << 4) |
                    (constAlpha << 8);

    bool cullOn = (d.cullMode != VK_CULL_MODE_NONE);
    uint32_t cullFaceVal = 5;  // VK_CULL_MODE_BACK_BIT default
    if (d.cullMode == VK_CULL_MODE_FRONT_AND_BACK) cullFaceVal = 8;
    else if (d.cullMode == VK_CULL_MODE_FRONT_BIT) cullFaceVal = 4;

    uint32_t primVal = 4;  // VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    if (d.topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST) primVal = 1;
    else if (d.topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST) primVal = 0;

    uint32_t frontBit = (d.frontFace == VK_FRONT_FACE_CLOCKWISE) ? 1u : 0u;
    k.rasterState = (cullOn ? 1u : 0u) | ((cullFaceVal & 0x0Fu) << 1) | ((primVal & 7u) << 5) | (frontBit << 8);
    return k;
}

inline DecodedPipelineKey decodePipelineKey(const PipelineKey& k) {
    DecodedPipelineKey d{};
    d.uid = k.uid;
    d.vtxFlags = k.flags;
    d.depthTest = (k.depthState & 1u) != 0;
    d.depthWrite = (k.depthState & 2u) != 0;
    d.depthCompareOp = (VkCompareOp)((k.depthState >> 4) & 7u);

    d.blendEnable = (k.blendState & 1u) != 0;
    d.srcColorBlendFactor = (VkBlendFactor)((k.blendState >> 1) & 0x1Fu);
    d.dstColorBlendFactor = (VkBlendFactor)((k.blendState >> 6) & 0x1Fu);
    d.srcAlphaBlendFactor = (VkBlendFactor)((k.blendState >> 11) & 0x1Fu);
    d.dstAlphaBlendFactor = (VkBlendFactor)((k.blendState >> 16) & 0x1Fu);

    d.colorBlendOp = (VkBlendOp)(k.blendState2 & 3u);
    d.alphaBlendOp = (VkBlendOp)((k.blendState2 >> 2) & 3u);
    uint32_t maskBits = (k.blendState2 >> 4) & 0x0Fu;
    d.colorWriteMask = 0;
    if (maskBits & 1) d.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
    if (maskBits & 2) d.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
    if (maskBits & 4) d.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
    if (maskBits & 8) d.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    d.blendConstantAlpha = ((float)((k.blendState2 >> 8) & 0xFF)) / 255.0f;

    bool cullOn = (k.rasterState & 1u) != 0;
    if (!cullOn) {
        d.cullMode = VK_CULL_MODE_NONE;
    } else {
        uint32_t cf = (k.rasterState >> 1) & 0x0Fu;
        if (cf == 8) d.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
        else if (cf == 4) d.cullMode = VK_CULL_MODE_FRONT_BIT;
        else d.cullMode = VK_CULL_MODE_BACK_BIT;
    }

    uint32_t primVal = (k.rasterState >> 5) & 7u;
    if (primVal == 1) d.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    else if (primVal == 0) d.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    else d.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    d.frontFace = (k.rasterState & (1u << 8)) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    return d;
}

struct SamplerDescKey {
    std::array<VkImageView, 8> views;
    std::array<VkSampler, 8> samplers;

    bool operator==(const SamplerDescKey& o) const {
        return views == o.views && samplers == o.samplers;
    }
};

struct SamplerDescKeyHash {
    size_t operator()(const SamplerDescKey& k) const {
        size_t h = 1469598103934665603ull;
        for (int i = 0; i < 8; i++) {
            h = (h ^ (uintptr_t)k.views[i]) * 1099511628211ull;
            h = (h ^ (uintptr_t)k.samplers[i]) * 1099511628211ull;
        }
        return h;
    }
};

struct SamplerSetEntry {
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint64_t lastUse = 0;
};

struct FrameResource {
    VkBuffer vbo = VK_NULL_HANDLE;
    VkDeviceMemory vboMem = VK_NULL_HANDLE;
    void* vboMapped = nullptr;
    size_t vboCap = 0;

    VkBuffer ibo = VK_NULL_HANDLE;
    VkDeviceMemory iboMem = VK_NULL_HANDLE;
    void* iboMapped = nullptr;
    size_t iboCap = 0;

    VkBuffer rowBuf = VK_NULL_HANDLE;
    VkDeviceMemory rowMem = VK_NULL_HANDLE;
    void* rowMapped = nullptr;
    size_t rowCap = 0;

    VkBuffer recBuf = VK_NULL_HANDLE;
    VkDeviceMemory recMem = VK_NULL_HANDLE;
    void* recMapped = nullptr;
    size_t recCap = 0;

    VkBuffer stBuf = VK_NULL_HANDLE;
    VkDeviceMemory stMem = VK_NULL_HANDLE;
    void* stMapped = nullptr;
    size_t stCap = 0;

    VkBuffer vtxRecBuf = VK_NULL_HANDLE;
    VkDeviceMemory vtxRecMem = VK_NULL_HANDLE;
    void* vtxRecMapped = nullptr;
    size_t vtxRecCap = 0;

    VkBuffer eyeUbo = VK_NULL_HANDLE;
    VkDeviceMemory eyeMem = VK_NULL_HANDLE;
    void* eyeMapped = nullptr;
    size_t eyeCap = 0;

    VkDescriptorSet set0 = VK_NULL_HANDLE;

    // UI dynamic vertex buffer
    VkBuffer uiVbo = VK_NULL_HANDLE;
    VkDeviceMemory uiVboMem = VK_NULL_HANDLE;
    void* uiVboMapped = nullptr;
    size_t uiVboCap = 0;
};

struct VkTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t levels = 1;
    uint32_t version = 0;
};

}  // namespace gpu
