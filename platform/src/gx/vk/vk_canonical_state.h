#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <android/log.h>

#ifndef LOGW
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "PetariVK", __VA_ARGS__)
#endif

namespace gpu {

enum class CanonicalDepthFunc : uint8_t {
    Never = 0, Less = 1, Equal = 2, LessOrEqual = 3,
    Greater = 4, NotEqual = 5, GreaterOrEqual = 6, Always = 7, Invalid = 255
};

enum class CanonicalBlendFactor : uint8_t {
    Zero = 0, One = 1, SrcColor = 2, OneMinusSrcColor = 3,
    SrcAlpha = 4, OneMinusSrcAlpha = 5, DstAlpha = 6, OneMinusDstAlpha = 7,
    DstColor = 8, OneMinusDstColor = 9, ConstantColor = 10, OneMinusConstantColor = 11,
    ConstantAlpha = 12, OneMinusConstantAlpha = 13, SrcAlphaSaturate = 14, Invalid = 255
};

enum class CanonicalBlendOp : uint8_t {
    Add = 0, Subtract = 1, ReverseSubtract = 2, Min = 3, Max = 4, Invalid = 255
};

enum class CanonicalCullMode : uint8_t {
    None = 0, Front = 1, Back = 2, FrontAndBack = 3, Invalid = 255
};

enum class CanonicalFrontFace : uint8_t {
    CounterClockwise = 0, Clockwise = 1, Invalid = 255
};

enum class CanonicalPrimitiveTopology : uint8_t {
    PointList = 0, LineList = 1, LineStrip = 2, TriangleList = 3,
    TriangleStrip = 4, TriangleFan = 5, Invalid = 255
};

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
    default: return VK_COMPARE_OP_LESS_OR_EQUAL;
    }
}

inline CanonicalDepthFunc toCanonicalDepthFunc(uint32_t raw) {
    switch (raw) {
    case 0x0200: return CanonicalDepthFunc::Never;
    case 0x0201: return CanonicalDepthFunc::Less;
    case 0x0202: return CanonicalDepthFunc::Equal;
    case 0x0203: return CanonicalDepthFunc::LessOrEqual;
    case 0x0204: return CanonicalDepthFunc::Greater;
    case 0x0205: return CanonicalDepthFunc::NotEqual;
    case 0x0206: return CanonicalDepthFunc::GreaterOrEqual;
    case 0x0207: return CanonicalDepthFunc::Always;
    default: return (raw <= 7) ? (CanonicalDepthFunc)raw : CanonicalDepthFunc::LessOrEqual;
    }
}
inline CanonicalDepthFunc toCanonicalDepthFuncFromGl(uint32_t gl) { return toCanonicalDepthFunc(gl); }

inline CanonicalDepthFunc toCanonicalDepthFuncFromVk(VkCompareOp op) {
    switch (op) {
    case VK_COMPARE_OP_NEVER: return CanonicalDepthFunc::Never;
    case VK_COMPARE_OP_LESS: return CanonicalDepthFunc::Less;
    case VK_COMPARE_OP_EQUAL: return CanonicalDepthFunc::Equal;
    case VK_COMPARE_OP_LESS_OR_EQUAL: return CanonicalDepthFunc::LessOrEqual;
    case VK_COMPARE_OP_GREATER: return CanonicalDepthFunc::Greater;
    case VK_COMPARE_OP_NOT_EQUAL: return CanonicalDepthFunc::NotEqual;
    case VK_COMPARE_OP_GREATER_OR_EQUAL: return CanonicalDepthFunc::GreaterOrEqual;
    case VK_COMPARE_OP_ALWAYS: return CanonicalDepthFunc::Always;
    default: return CanonicalDepthFunc::LessOrEqual;
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
    default: return VK_BLEND_FACTOR_ONE;
    }
}

inline CanonicalBlendFactor toCanonicalBlendFactor(uint32_t raw) {
    switch (raw) {
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
    default: return (raw <= 14) ? (CanonicalBlendFactor)raw : CanonicalBlendFactor::One;
    }
}
inline CanonicalBlendFactor toCanonicalBlendFactorFromGl(uint32_t gl) { return toCanonicalBlendFactor(gl); }

inline CanonicalBlendFactor toCanonicalBlendFactorFromVk(VkBlendFactor factor) {
    switch (factor) {
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
    default: return CanonicalBlendFactor::One;
    }
}

inline VkBlendOp toVkBlendOp(CanonicalBlendOp op) {
    switch (op) {
    case CanonicalBlendOp::Add: return VK_BLEND_OP_ADD;
    case CanonicalBlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
    case CanonicalBlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case CanonicalBlendOp::Min: return VK_BLEND_OP_MIN;
    case CanonicalBlendOp::Max: return VK_BLEND_OP_MAX;
    default: return VK_BLEND_OP_ADD;
    }
}

inline CanonicalBlendOp toCanonicalBlendOp(uint32_t raw) {
    switch (raw) {
    // GX CMODE0 bit 11 selects dst-src blending; gx_builder tags that case
    // with 0x8008 (see GDPixel.c PE_CMODE0 and GX_BM_SUBTRACT).  It is not
    // GL_MAX, despite sharing that constant's value.
    case 0x8006: return CanonicalBlendOp::Add;
    case 0x8008: return CanonicalBlendOp::ReverseSubtract;
    case 0x800A: return CanonicalBlendOp::Subtract;
    case 0x800B: return CanonicalBlendOp::ReverseSubtract;
    case 0x8007: return CanonicalBlendOp::Min;
    default: return (raw <= 4) ? (CanonicalBlendOp)raw : CanonicalBlendOp::Add;
    }
}
inline CanonicalBlendOp toCanonicalBlendOpFromGl(uint32_t gl) { return toCanonicalBlendOp(gl); }

inline CanonicalBlendOp toCanonicalBlendOpFromVk(VkBlendOp op) {
    switch (op) {
    case VK_BLEND_OP_ADD: return CanonicalBlendOp::Add;
    case VK_BLEND_OP_SUBTRACT: return CanonicalBlendOp::Subtract;
    case VK_BLEND_OP_REVERSE_SUBTRACT: return CanonicalBlendOp::ReverseSubtract;
    case VK_BLEND_OP_MIN: return CanonicalBlendOp::Min;
    case VK_BLEND_OP_MAX: return CanonicalBlendOp::Max;
    default: return CanonicalBlendOp::Add;
    }
}

inline VkCullModeFlags toVkCullMode(CanonicalCullMode mode) {
    switch (mode) {
    case CanonicalCullMode::None: return VK_CULL_MODE_NONE;
    case CanonicalCullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CanonicalCullMode::Back: return VK_CULL_MODE_BACK_BIT;
    case CanonicalCullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
    default: return VK_CULL_MODE_BACK_BIT;
    }
}

inline CanonicalCullMode toCanonicalCullMode(bool cullOn, uint32_t rawFace) {
    if (!cullOn) return CanonicalCullMode::None;
    switch (rawFace) {
    case 0x0404: return CanonicalCullMode::Front;
    case 0x0405: return CanonicalCullMode::Back;
    case 0x0408: return CanonicalCullMode::FrontAndBack;
    default: return (rawFace <= 3) ? (CanonicalCullMode)rawFace : CanonicalCullMode::Back;
    }
}
inline CanonicalCullMode toCanonicalCullMode(uint32_t raw) {
    return toCanonicalCullMode(raw != 0, raw);
}
inline CanonicalCullMode toCanonicalCullModeFromGl(bool cullOn, uint32_t glFace) {
    return toCanonicalCullMode(cullOn, glFace);
}
inline CanonicalCullMode toCanonicalCullModeFromGl(uint32_t gl) {
    return toCanonicalCullMode(gl);
}

inline CanonicalCullMode toCanonicalCullModeFromVk(VkCullModeFlags flags) {
    if ((flags & VK_CULL_MODE_FRONT_AND_BACK) == VK_CULL_MODE_FRONT_AND_BACK) return CanonicalCullMode::FrontAndBack;
    if (flags & VK_CULL_MODE_FRONT_BIT) return CanonicalCullMode::Front;
    if (flags & VK_CULL_MODE_BACK_BIT) return CanonicalCullMode::Back;
    return CanonicalCullMode::None;
}

inline VkFrontFace toVkFrontFace(CanonicalFrontFace ff) {
    return (ff == CanonicalFrontFace::Clockwise) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

inline CanonicalFrontFace toCanonicalFrontFace(uint32_t raw) {
    return (raw == 0x0900) ? CanonicalFrontFace::Clockwise : CanonicalFrontFace::CounterClockwise;
}
inline CanonicalFrontFace toCanonicalFrontFaceFromGl(uint32_t gl) { return toCanonicalFrontFace(gl); }

inline CanonicalFrontFace toCanonicalFrontFaceFromVk(VkFrontFace ff) {
    return (ff == VK_FRONT_FACE_CLOCKWISE) ? CanonicalFrontFace::Clockwise : CanonicalFrontFace::CounterClockwise;
}

inline VkPrimitiveTopology toVkTopology(CanonicalPrimitiveTopology topo) {
    switch (topo) {
    case CanonicalPrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case CanonicalPrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case CanonicalPrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case CanonicalPrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case CanonicalPrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case CanonicalPrimitiveTopology::TriangleFan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

inline CanonicalPrimitiveTopology toCanonicalTopology(uint32_t raw) {
    switch (raw) {
    case 0x0000: return CanonicalPrimitiveTopology::PointList;
    case 0x0001: return CanonicalPrimitiveTopology::LineList;
    case 0x0003: return CanonicalPrimitiveTopology::LineStrip;
    case 0x0004: return CanonicalPrimitiveTopology::TriangleList;
    case 0x0005: return CanonicalPrimitiveTopology::TriangleStrip;
    case 0x0006: return CanonicalPrimitiveTopology::TriangleFan;
    default: return (raw <= 5) ? (CanonicalPrimitiveTopology)raw : CanonicalPrimitiveTopology::TriangleList;
    }
}
inline CanonicalPrimitiveTopology toCanonicalTopologyFromGl(uint32_t gl) { return toCanonicalTopology(gl); }

inline CanonicalPrimitiveTopology toCanonicalTopologyFromVk(VkPrimitiveTopology topo) {
    switch (topo) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return CanonicalPrimitiveTopology::PointList;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: return CanonicalPrimitiveTopology::LineList;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: return CanonicalPrimitiveTopology::LineStrip;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return CanonicalPrimitiveTopology::TriangleList;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return CanonicalPrimitiveTopology::TriangleStrip;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: return CanonicalPrimitiveTopology::TriangleFan;
    default: return CanonicalPrimitiveTopology::TriangleList;
    }
}

}  // namespace gpu
