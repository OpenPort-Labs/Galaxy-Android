#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cstddef>
#include "gx/gx_record.h"
#include "gx/vk_shadergen.h"
#include <android/log.h>

#ifndef LOGW
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "PetariVK", __VA_ARGS__)
#endif

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

// Explicit canonical state enums representing GX and Vulkan rendering state
enum class CanonicalDepthFunc : uint8_t {
    Never = 0,
    Less = 1,
    Equal = 2,
    LessOrEqual = 3,
    Greater = 4,
    NotEqual = 5,
    GreaterOrEqual = 6,
    Always = 7,
    Invalid = 255
};

enum class CanonicalBlendFactor : uint8_t {
    Zero = 0,
    One = 1,
    SrcColor = 2,
    OneMinusSrcColor = 3,
    SrcAlpha = 4,
    OneMinusSrcAlpha = 5,
    DstAlpha = 6,
    OneMinusDstAlpha = 7,
    DstColor = 8,
    OneMinusDstColor = 9,
    ConstantColor = 10,
    OneMinusConstantColor = 11,
    ConstantAlpha = 12,
    OneMinusConstantAlpha = 13,
    SrcAlphaSaturate = 14,
    Invalid = 255
};

enum class CanonicalBlendOp : uint8_t {
    Add = 0,
    Subtract = 1,
    ReverseSubtract = 2,
    Min = 3,
    Max = 4,
    Invalid = 255
};

enum class CanonicalCullMode : uint8_t {
    None = 0,
    Front = 1,
    Back = 2,
    FrontAndBack = 3,
    Invalid = 255
};

enum class CanonicalFrontFace : uint8_t {
    CounterClockwise = 0,
    Clockwise = 1,
    Invalid = 255
};

enum class CanonicalPrimitiveTopology : uint8_t {
    PointList = 0,
    LineList = 1,
    LineStrip = 2,
    TriangleList = 3,
    TriangleStrip = 4,
    TriangleFan = 5,
    Invalid = 255
};

// Two-way explicit conversion functions
inline VkCompareOp toVkCompareOp(CanonicalDepthFunc func) {
    switch (func) {
    case CanonicalDepthFunc::Never: return VK_COMPARE_OP_NEVER;
    case CanonicalDepthFunc::Less: return VK_COMPARE_OP_LESS;
    case CanonicalDepthFunc::Equal: return VK_COMPARE_OP_EQUAL;
    case CanonicalDepthFunc::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CanonicalDepthFunc::Greater: return VK_COMPARE_OP_GREATER;
    case CanonicalDepthFunc::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    case CanonicalDepthFunc::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CanonicalDepthFunc::Always: return VK_COMPARE_OP_ALWAYS;
    default:
        LOGW("toVkCompareOp: invalid CanonicalDepthFunc %d, defaulting to VK_COMPARE_OP_LESS_OR_EQUAL", (int)func);
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    }
}

inline CanonicalDepthFunc toCanonicalDepthFuncFromGl(uint32_t gl) {
    switch (gl) {
    case 0x0200: return CanonicalDepthFunc::Never;
    case 0x0201: return CanonicalDepthFunc::Less;
    case 0x0202: return CanonicalDepthFunc::Equal;
    case 0x0203: return CanonicalDepthFunc::LessOrEqual;
    case 0x0204: return CanonicalDepthFunc::Greater;
    case 0x0205: return CanonicalDepthFunc::NotEqual;
    case 0x0206: return CanonicalDepthFunc::GreaterOrEqual;
    case 0x0207: return CanonicalDepthFunc::Always;
    default:
        if (gl <= 7) return (CanonicalDepthFunc)gl;
        LOGW("toCanonicalDepthFuncFromGl: invalid GL depth func 0x%x, defaulting to LessOrEqual", gl);
        return CanonicalDepthFunc::LessOrEqual;
    }
}

inline CanonicalDepthFunc toCanonicalDepthFuncFromGx(uint32_t gx) {
    if (gx <= 7) return (CanonicalDepthFunc)gx;
    LOGW("toCanonicalDepthFuncFromGx: invalid GX compare func %u, defaulting to LessOrEqual", gx);
    return CanonicalDepthFunc::LessOrEqual;
}

inline CanonicalDepthFunc toCanonicalDepthFuncFromVk(VkCompareOp vk) {
    switch (vk) {
    case VK_COMPARE_OP_NEVER: return CanonicalDepthFunc::Never;
    case VK_COMPARE_OP_LESS: return CanonicalDepthFunc::Less;
    case VK_COMPARE_OP_EQUAL: return CanonicalDepthFunc::Equal;
    case VK_COMPARE_OP_LESS_OR_EQUAL: return CanonicalDepthFunc::LessOrEqual;
    case VK_COMPARE_OP_GREATER: return CanonicalDepthFunc::Greater;
    case VK_COMPARE_OP_NOT_EQUAL: return CanonicalDepthFunc::NotEqual;
    case VK_COMPARE_OP_GREATER_OR_EQUAL: return CanonicalDepthFunc::GreaterOrEqual;
    case VK_COMPARE_OP_ALWAYS: return CanonicalDepthFunc::Always;
    default:
        LOGW("toCanonicalDepthFuncFromVk: unknown VkCompareOp %d, defaulting to LessOrEqual", (int)vk);
        return CanonicalDepthFunc::LessOrEqual;
    }
}

inline VkBlendFactor toVkBlendFactor(CanonicalBlendFactor factor) {
    switch (factor) {
    case CanonicalBlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
    case CanonicalBlendFactor::One: return VK_BLEND_FACTOR_ONE;
    case CanonicalBlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
    case CanonicalBlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case CanonicalBlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
    case CanonicalBlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case CanonicalBlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
    case CanonicalBlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case CanonicalBlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
    case CanonicalBlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case CanonicalBlendFactor::ConstantColor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case CanonicalBlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case CanonicalBlendFactor::ConstantAlpha: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case CanonicalBlendFactor::OneMinusConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    case CanonicalBlendFactor::SrcAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    default:
        LOGW("toVkBlendFactor: invalid CanonicalBlendFactor %d, defaulting to VK_BLEND_FACTOR_ONE", (int)factor);
        return VK_BLEND_FACTOR_ONE;
    }
}

inline CanonicalBlendFactor toCanonicalBlendFactorFromGl(uint32_t gl) {
    switch (gl) {
    case 0: return CanonicalBlendFactor::Zero;
    case 1: return CanonicalBlendFactor::One;
    case 0x0300: return CanonicalBlendFactor::SrcColor;
    case 0x0301: return CanonicalBlendFactor::OneMinusSrcColor;
    case 0x0302: return CanonicalBlendFactor::SrcAlpha;
    case 0x0303: return CanonicalBlendFactor::OneMinusSrcAlpha;
    case 0x0304: return CanonicalBlendFactor::DstAlpha;
    case 0x0305: return CanonicalBlendFactor::OneMinusDstAlpha;
    case 0x0306: return CanonicalBlendFactor::DstColor;
    case 0x0307: return CanonicalBlendFactor::OneMinusDstColor;
    case 0x8001: return CanonicalBlendFactor::ConstantColor;
    case 0x8002: return CanonicalBlendFactor::OneMinusConstantColor;
    case 0x8003: return CanonicalBlendFactor::ConstantAlpha;
    case 0x8004: return CanonicalBlendFactor::OneMinusConstantAlpha;
    case 0x0308: return CanonicalBlendFactor::SrcAlphaSaturate;
    default:
        LOGW("toCanonicalBlendFactorFromGl: unknown GL blend factor 0x%x, defaulting to One", gl);
        return CanonicalBlendFactor::One;
    }
}

inline CanonicalBlendFactor toCanonicalBlendFactorFromGx(uint32_t gx) {
    switch (gx) {
    case 0: return CanonicalBlendFactor::Zero;
    case 1: return CanonicalBlendFactor::One;
    case 2: return CanonicalBlendFactor::SrcColor;
    case 3: return CanonicalBlendFactor::OneMinusSrcColor;
    case 4: return CanonicalBlendFactor::SrcAlpha;
    case 5: return CanonicalBlendFactor::OneMinusSrcAlpha;
    case 6: return CanonicalBlendFactor::DstAlpha;
    case 7: return CanonicalBlendFactor::OneMinusDstAlpha;
    default:
        LOGW("toCanonicalBlendFactorFromGx: unknown GX blend factor %u, defaulting to One", gx);
        return CanonicalBlendFactor::One;
    }
}

inline CanonicalBlendFactor toCanonicalBlendFactorFromVk(VkBlendFactor vk) {
    switch (vk) {
    case VK_BLEND_FACTOR_ZERO: return CanonicalBlendFactor::Zero;
    case VK_BLEND_FACTOR_ONE: return CanonicalBlendFactor::One;
    case VK_BLEND_FACTOR_SRC_COLOR: return CanonicalBlendFactor::SrcColor;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return CanonicalBlendFactor::OneMinusSrcColor;
    case VK_BLEND_FACTOR_SRC_ALPHA: return CanonicalBlendFactor::SrcAlpha;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return CanonicalBlendFactor::OneMinusSrcAlpha;
    case VK_BLEND_FACTOR_DST_ALPHA: return CanonicalBlendFactor::DstAlpha;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return CanonicalBlendFactor::OneMinusDstAlpha;
    case VK_BLEND_FACTOR_DST_COLOR: return CanonicalBlendFactor::DstColor;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return CanonicalBlendFactor::OneMinusDstColor;
    case VK_BLEND_FACTOR_CONSTANT_COLOR: return CanonicalBlendFactor::ConstantColor;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR: return CanonicalBlendFactor::OneMinusConstantColor;
    case VK_BLEND_FACTOR_CONSTANT_ALPHA: return CanonicalBlendFactor::ConstantAlpha;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA: return CanonicalBlendFactor::OneMinusConstantAlpha;
    case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE: return CanonicalBlendFactor::SrcAlphaSaturate;
    default:
        LOGW("toCanonicalBlendFactorFromVk: unknown VkBlendFactor %d, defaulting to One", (int)vk);
        return CanonicalBlendFactor::One;
    }
}

inline VkBlendOp toVkBlendOp(CanonicalBlendOp op) {
    switch (op) {
    case CanonicalBlendOp::Add: return VK_BLEND_OP_ADD;
    case CanonicalBlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
    case CanonicalBlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case CanonicalBlendOp::Min: return VK_BLEND_OP_MIN;
    case CanonicalBlendOp::Max: return VK_BLEND_OP_MAX;
    default:
        LOGW("toVkBlendOp: invalid CanonicalBlendOp %d, defaulting to VK_BLEND_OP_ADD", (int)op);
        return VK_BLEND_OP_ADD;
    }
}

inline CanonicalBlendOp toCanonicalBlendOpFromGl(uint32_t gl) {
    switch (gl) {
    case 0x8006: return CanonicalBlendOp::Add;
    case 0x8007: return CanonicalBlendOp::Subtract;
    case 0x8008: return CanonicalBlendOp::ReverseSubtract;
    case 0x800B: return CanonicalBlendOp::Min;
    case 0x800C: return CanonicalBlendOp::Max;
    default:
        LOGW("toCanonicalBlendOpFromGl: unknown GL blend op 0x%x, defaulting to Add", gl);
        return CanonicalBlendOp::Add;
    }
}

inline CanonicalBlendOp toCanonicalBlendOpFromGx(uint32_t gx) {
    switch (gx) {
    case 0: return CanonicalBlendOp::Add;
    case 1: return CanonicalBlendOp::Subtract;
    case 2: return CanonicalBlendOp::ReverseSubtract;
    default:
        LOGW("toCanonicalBlendOpFromGx: unknown GX blend op %u, defaulting to Add", gx);
        return CanonicalBlendOp::Add;
    }
}

inline CanonicalBlendOp toCanonicalBlendOpFromVk(VkBlendOp vk) {
    switch (vk) {
    case VK_BLEND_OP_ADD: return CanonicalBlendOp::Add;
    case VK_BLEND_OP_SUBTRACT: return CanonicalBlendOp::Subtract;
    case VK_BLEND_OP_REVERSE_SUBTRACT: return CanonicalBlendOp::ReverseSubtract;
    case VK_BLEND_OP_MIN: return CanonicalBlendOp::Min;
    case VK_BLEND_OP_MAX: return CanonicalBlendOp::Max;
    default:
        LOGW("toCanonicalBlendOpFromVk: unknown VkBlendOp %d, defaulting to Add", (int)vk);
        return CanonicalBlendOp::Add;
    }
}

inline VkCullModeFlags toVkCullMode(CanonicalCullMode cull) {
    switch (cull) {
    case CanonicalCullMode::None: return VK_CULL_MODE_NONE;
    case CanonicalCullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CanonicalCullMode::Back: return VK_CULL_MODE_BACK_BIT;
    case CanonicalCullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
    default:
        LOGW("toVkCullMode: invalid CanonicalCullMode %d, defaulting to VK_CULL_MODE_BACK_BIT", (int)cull);
        return VK_CULL_MODE_BACK_BIT;
    }
}

inline CanonicalCullMode toCanonicalCullModeFromGl(bool cullOn, uint32_t glFace) {
    if (!cullOn) return CanonicalCullMode::None;
    switch (glFace) {
    case 0x0404: return CanonicalCullMode::Front;
    case 0x0405: return CanonicalCullMode::Back;
    case 0x0408: return CanonicalCullMode::FrontAndBack;
    default:
        LOGW("toCanonicalCullModeFromGl: unknown GL cull face 0x%x, defaulting to Back", glFace);
        return CanonicalCullMode::Back;
    }
}

inline CanonicalCullMode toCanonicalCullModeFromGx(uint32_t gx) {
    switch (gx) {
    case 0: return CanonicalCullMode::None;
    case 1: return CanonicalCullMode::Front;
    case 2: return CanonicalCullMode::Back;
    case 3: return CanonicalCullMode::FrontAndBack;
    default:
        LOGW("toCanonicalCullModeFromGx: unknown GX cull mode %u, defaulting to Back", gx);
        return CanonicalCullMode::Back;
    }
}

inline CanonicalCullMode toCanonicalCullModeFromVk(VkCullModeFlags vk) {
    if (vk == VK_CULL_MODE_NONE) return CanonicalCullMode::None;
    if (vk == VK_CULL_MODE_FRONT_BIT) return CanonicalCullMode::Front;
    if (vk == VK_CULL_MODE_BACK_BIT) return CanonicalCullMode::Back;
    if (vk == VK_CULL_MODE_FRONT_AND_BACK) return CanonicalCullMode::FrontAndBack;
    LOGW("toCanonicalCullModeFromVk: unknown VkCullModeFlags 0x%x, defaulting to Back", vk);
    return CanonicalCullMode::Back;
}

inline VkPrimitiveTopology toVkTopology(CanonicalPrimitiveTopology topo) {
    switch (topo) {
    case CanonicalPrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case CanonicalPrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case CanonicalPrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case CanonicalPrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case CanonicalPrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case CanonicalPrimitiveTopology::TriangleFan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    default:
        LOGW("toVkTopology: invalid CanonicalPrimitiveTopology %d, defaulting to TriangleList", (int)topo);
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

inline CanonicalPrimitiveTopology toCanonicalTopologyFromGl(uint32_t gl) {
    switch (gl) {
    case 0: return CanonicalPrimitiveTopology::PointList;
    case 1: return CanonicalPrimitiveTopology::LineList;
    case 2:
    case 3: return CanonicalPrimitiveTopology::LineStrip;
    case 4: return CanonicalPrimitiveTopology::TriangleList;
    case 5: return CanonicalPrimitiveTopology::TriangleStrip;
    case 6: return CanonicalPrimitiveTopology::TriangleFan;
    default:
        LOGW("toCanonicalTopologyFromGl: unknown GL prim %u, defaulting to TriangleList", gl);
        return CanonicalPrimitiveTopology::TriangleList;
    }
}

inline CanonicalPrimitiveTopology toCanonicalTopologyFromGx(uint32_t gx) {
    switch (gx) {
    case 0: return CanonicalPrimitiveTopology::PointList;
    case 1: return CanonicalPrimitiveTopology::LineList;
    case 2: return CanonicalPrimitiveTopology::LineStrip;
    case 3:
    case 4: return CanonicalPrimitiveTopology::TriangleList;
    case 5: return CanonicalPrimitiveTopology::TriangleStrip;
    case 6: return CanonicalPrimitiveTopology::TriangleFan;
    default:
        LOGW("toCanonicalTopologyFromGx: unknown GX prim %u, defaulting to TriangleList", gx);
        return CanonicalPrimitiveTopology::TriangleList;
    }
}

inline CanonicalPrimitiveTopology toCanonicalTopologyFromVk(VkPrimitiveTopology vk) {
    switch (vk) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return CanonicalPrimitiveTopology::PointList;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: return CanonicalPrimitiveTopology::LineList;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: return CanonicalPrimitiveTopology::LineStrip;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return CanonicalPrimitiveTopology::TriangleList;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return CanonicalPrimitiveTopology::TriangleStrip;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: return CanonicalPrimitiveTopology::TriangleFan;
    default:
        LOGW("toCanonicalTopologyFromVk: unknown VkPrimitiveTopology %d, defaulting to TriangleList", (int)vk);
        return CanonicalPrimitiveTopology::TriangleList;
    }
}

inline VkFrontFace toVkFrontFace(CanonicalFrontFace ff) {
    return (ff == CanonicalFrontFace::Clockwise) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

inline CanonicalFrontFace toCanonicalFrontFaceFromVk(VkFrontFace vk) {
    return (vk == VK_FRONT_FACE_CLOCKWISE) ? CanonicalFrontFace::Clockwise : CanonicalFrontFace::CounterClockwise;
}

// Canonical decoded state representation
struct DecodedPipelineKey {
    ShaderUid uid{};
    uint32_t vtxFlags = 0;

    // Depth state
    bool depthTest = false;
    bool depthWrite = false;
    CanonicalDepthFunc depthCompareOp = CanonicalDepthFunc::LessOrEqual;

    // Blend state
    bool blendEnable = false;
    CanonicalBlendFactor srcColorBlendFactor = CanonicalBlendFactor::One;
    CanonicalBlendFactor dstColorBlendFactor = CanonicalBlendFactor::Zero;
    CanonicalBlendOp colorBlendOp = CanonicalBlendOp::Add;
    CanonicalBlendFactor srcAlphaBlendFactor = CanonicalBlendFactor::One;
    CanonicalBlendFactor dstAlphaBlendFactor = CanonicalBlendFactor::Zero;
    CanonicalBlendOp alphaBlendOp = CanonicalBlendOp::Add;
    uint32_t colorWriteMask = 0x0F;  // RGBA bits 0..3
    float blendConstantAlpha = 0.0f;

    // Raster state
    CanonicalCullMode cullMode = CanonicalCullMode::Back;
    CanonicalFrontFace frontFace = CanonicalFrontFace::CounterClockwise;
    CanonicalPrimitiveTopology topology = CanonicalPrimitiveTopology::TriangleList;
};

inline PipelineKey encodePipelineKey(const DecodedPipelineKey& d) {
    PipelineKey k{};
    k.uid = d.uid;
    k.flags = d.vtxFlags;

    uint32_t depthFuncVal = (uint32_t)d.depthCompareOp & 0x0Fu;
    k.depthState = (d.depthTest ? 1u : 0u) |
                   ((d.depthTest && d.depthWrite ? 1u : 0u) << 1) |
                   (depthFuncVal << 4);

    uint32_t srcColor = (uint32_t)d.srcColorBlendFactor & 0x1Fu;
    uint32_t dstColor = (uint32_t)d.dstColorBlendFactor & 0x1Fu;
    uint32_t srcAlpha = (uint32_t)d.srcAlphaBlendFactor & 0x1Fu;
    uint32_t dstAlpha = (uint32_t)d.dstAlphaBlendFactor & 0x1Fu;
    k.blendState = (d.blendEnable ? 1u : 0u) |
                   (srcColor << 1) |
                   (dstColor << 6) |
                   (srcAlpha << 11) |
                   (dstAlpha << 16);

    uint32_t colorOp = (uint32_t)d.colorBlendOp & 7u;
    uint32_t alphaOp = (uint32_t)d.alphaBlendOp & 7u;
    uint32_t maskBits = d.colorWriteMask & 0x0Fu;
    float clampedAlpha = (d.blendConstantAlpha < 0.0f) ? 0.0f : ((d.blendConstantAlpha > 1.0f) ? 1.0f : d.blendConstantAlpha);
    uint32_t constAlpha = (uint32_t)(clampedAlpha * 255.0f + 0.5f) & 0xFF;

    k.blendState2 = colorOp |
                    (alphaOp << 3) |
                    (maskBits << 6) |
                    (constAlpha << 10);

    uint32_t cullVal = (uint32_t)d.cullMode & 3u;
    uint32_t primVal = (uint32_t)d.topology & 7u;
    uint32_t frontBit = (d.frontFace == CanonicalFrontFace::Clockwise) ? 1u : 0u;
    k.rasterState = cullVal | (primVal << 2) | (frontBit << 5);

    return k;
}

inline DecodedPipelineKey decodePipelineKey(const PipelineKey& k) {
    DecodedPipelineKey d{};
    d.uid = k.uid;
    d.vtxFlags = k.flags;

    d.depthTest = (k.depthState & 1u) != 0;
    d.depthWrite = (k.depthState & 2u) != 0;
    d.depthCompareOp = (CanonicalDepthFunc)((k.depthState >> 4) & 0x0Fu);

    d.blendEnable = (k.blendState & 1u) != 0;
    d.srcColorBlendFactor = (CanonicalBlendFactor)((k.blendState >> 1) & 0x1Fu);
    d.dstColorBlendFactor = (CanonicalBlendFactor)((k.blendState >> 6) & 0x1Fu);
    d.srcAlphaBlendFactor = (CanonicalBlendFactor)((k.blendState >> 11) & 0x1Fu);
    d.dstAlphaBlendFactor = (CanonicalBlendFactor)((k.blendState >> 16) & 0x1Fu);

    d.colorBlendOp = (CanonicalBlendOp)(k.blendState2 & 7u);
    d.alphaBlendOp = (CanonicalBlendOp)((k.blendState2 >> 3) & 7u);
    d.colorWriteMask = (k.blendState2 >> 6) & 0x0Fu;
    d.blendConstantAlpha = ((float)((k.blendState2 >> 10) & 0xFF)) / 255.0f;

    d.cullMode = (CanonicalCullMode)(k.rasterState & 3u);
    d.topology = (CanonicalPrimitiveTopology)((k.rasterState >> 2) & 7u);
    d.frontFace = (k.rasterState & (1u << 5)) ? CanonicalFrontFace::Clockwise : CanonicalFrontFace::CounterClockwise;

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
