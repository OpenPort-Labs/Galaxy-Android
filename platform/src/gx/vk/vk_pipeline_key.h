#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include "gx/gx_shader_uid.h"
#include "gx/vk/vk_canonical_state.h"

namespace gpu {

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

struct DecodedPipelineKey {
    ShaderUid uid;
    uint32_t vtxFlags = 0;

    bool depthTest = true;
    bool depthWrite = true;
    CanonicalDepthFunc depthCompareOp = CanonicalDepthFunc::LessOrEqual;

    bool blendEnable = false;
    CanonicalBlendFactor srcColorBlendFactor = CanonicalBlendFactor::One;
    CanonicalBlendFactor dstColorBlendFactor = CanonicalBlendFactor::Zero;
    CanonicalBlendOp colorBlendOp = CanonicalBlendOp::Add;
    CanonicalBlendFactor srcAlphaBlendFactor = CanonicalBlendFactor::One;
    CanonicalBlendFactor dstAlphaBlendFactor = CanonicalBlendFactor::Zero;
    CanonicalBlendOp alphaBlendOp = CanonicalBlendOp::Add;
    uint32_t colorWriteMask = 0x0F;
    float blendConstantAlpha = 0.0f;

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

}  // namespace gpu
