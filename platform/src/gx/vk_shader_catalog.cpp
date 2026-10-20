#include "gx/vk_shader_catalog.h"
#include "gx/vk_shader_inventory.h"
#include "port/port.h"

namespace gpu {

PipelineKey makeCanonicalPipelineKey(const ShaderUid& uid, uint32_t vtxFlags, bool depthTest,
                                     bool depthWrite, VkCompareOp depthOp, bool blendEnable,
                                     VkBlendFactor srcColor, VkBlendFactor dstColor,
                                     VkBlendFactor srcAlpha, VkBlendFactor dstAlpha,
                                     VkCullModeFlags cullMode,
                                     VkPrimitiveTopology topology) {
    DecodedPipelineKey d{};
    d.uid = uid;
    d.vtxFlags = vtxFlags;
    d.depthTest = depthTest;
    d.depthWrite = depthWrite;
    d.depthCompareOp = toCanonicalDepthFuncFromVk(depthOp);
    d.blendEnable = blendEnable;
    d.srcColorBlendFactor = toCanonicalBlendFactorFromVk(srcColor);
    d.dstColorBlendFactor = toCanonicalBlendFactorFromVk(dstColor);
    d.colorBlendOp = CanonicalBlendOp::Add;
    d.srcAlphaBlendFactor = toCanonicalBlendFactorFromVk(srcAlpha);
    d.dstAlphaBlendFactor = toCanonicalBlendFactorFromVk(dstAlpha);
    d.alphaBlendOp = CanonicalBlendOp::Add;
    d.colorWriteMask = 0x0F;
    d.blendConstantAlpha = 0.0f;
    d.cullMode = toCanonicalCullModeFromVk(cullMode);
    d.frontFace = CanonicalFrontFace::CounterClockwise;
    d.topology = toCanonicalTopologyFromVk(topology);
    return encodePipelineKey(d);
}

void populateCanonicalGalaxyCatalog(ShaderInventory& inv) {
    const uint32_t vtxLayouts[] = {
        0x0001, 0x0041, 0x0051, 0x0045, 0x0055,
        0x0047, 0x0057, 0x4047, 0x4057, 0x40C7
    };

    // 1. Placeholder stubs
    for (uint32_t vf : vtxLayouts) {
        ShaderUid stub{};
        stub.vtxFlags = vf;
        PipelineKey k1 = makeCanonicalPipelineKey(stub, vf, true, true, VK_COMPARE_OP_LESS_OR_EQUAL,
                                                 false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                 VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                 VK_CULL_MODE_BACK_BIT);
        PipelineKey k2 = makeCanonicalPipelineKey(stub, vf, false, false, VK_COMPARE_OP_ALWAYS,
                                                 false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                 VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                 VK_CULL_MODE_NONE);
        inv.addEntry(stub, k1, "placeholder_stub", false);
        inv.addEntry(stub, k2, "placeholder_stub", false);
    }

    // 2. Title screen, logo, banners, flashcards, UI
    for (uint32_t chans = 0; chans <= 1; chans++) {
        for (uint32_t blendOn = 0; blendOn <= 1; blendOn++) {
            ShaderUid u{};
            u.vtxFlags = 0x0051;
            u.hud = 1;
            u.numChans = chans;
            u.numTexGens = 1;
            u.numTevStages = 1;
            u.texMtxInfo[0] = 5 << 7;
            if (chans) {
                u.colorCtrl[0] = 0x0001;
                u.alphaCtrl[0] = 0x0001;
            }
            u.tevColor[0] = 0x080A00;
            u.tevAlpha[0] = 0x080A00;

            VkBlendFactor srcF = blendOn ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
            VkBlendFactor dstF = blendOn ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO;

            PipelineKey k = makeCanonicalPipelineKey(u, u.vtxFlags, false, false, VK_COMPARE_OP_ALWAYS,
                                                    blendOn != 0, srcF, dstF, srcF, dstF,
                                                    VK_CULL_MODE_NONE);
            inv.addEntry(u, k, "title_screen_and_ui", false);

            PipelineKey kDepth = makeCanonicalPipelineKey(u, u.vtxFlags, true, true, VK_COMPARE_OP_LESS_OR_EQUAL,
                                                         blendOn != 0, srcF, dstF, srcF, dstF,
                                                         VK_CULL_MODE_BACK_BIT);
            inv.addEntry(u, kDepth, "title_screen_3d_logo", false);
        }
    }

    // 3. Skybox and Star Dome
    for (uint32_t tg = 1; tg <= 2; tg++) {
        for (uint32_t cull = 0; cull <= 1; cull++) {
            ShaderUid u{};
            u.vtxFlags = 0x0041;
            u.numChans = 0;
            u.numTexGens = tg;
            u.numTevStages = 1;
            u.texMtxInfo[0] = 5 << 7;
            u.tevColor[0] = 0x080A00;
            u.tevAlpha[0] = 0x080A00;
            VkCullModeFlags cm = cull ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE;
            PipelineKey k = makeCanonicalPipelineKey(u, u.vtxFlags, false, false, VK_COMPARE_OP_ALWAYS,
                                                    false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                    VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, cm);
            inv.addEntry(u, k, "skybox_and_dome", false);
        }
    }

    // 4. Character models (Mario, Rosalina, NPCs)
    for (uint32_t cull = 0; cull <= 1; cull++) {
        for (uint32_t blendMode = 0; blendMode <= 1; blendMode++) {
            ShaderUid u{};
            u.vtxFlags = 0x4057;
            u.numChans = 1;
            u.numTexGens = 1;
            u.numTevStages = 1;
            u.colorCtrl[0] = 0x0001;
            u.texMtxInfo[0] = 5 << 7;
            u.tevColor[0] = 0x080A00;
            u.tevAlpha[0] = 0x080A00;

            VkCullModeFlags cm = cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
            VkBlendFactor srcF = VK_BLEND_FACTOR_SRC_ALPHA;
            VkBlendFactor dstF = (blendMode == 0) ? VK_BLEND_FACTOR_ZERO : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

            PipelineKey k = makeCanonicalPipelineKey(u, u.vtxFlags, true, true, VK_COMPARE_OP_LESS_OR_EQUAL,
                                                    blendMode != 0, srcF, dstF, srcF, dstF, cm);
            inv.addEntry(u, k, "character_models", false);
        }
    }

    // 5. Planet Terrain and Static Geometry
    for (uint32_t alphaTest = 0; alphaTest <= 1; alphaTest++) {
        for (uint32_t tg = 1; tg <= 2; tg++) {
            ShaderUid u{};
            u.vtxFlags = 0x0057;
            u.numChans = 1;
            u.numTexGens = tg;
            u.numTevStages = 1;
            u.colorCtrl[0] = 0x0001;
            u.texMtxInfo[0] = 5 << 7;
            u.tevColor[0] = 0x080A00;
            u.tevAlpha[0] = 0x080A00;
            if (alphaTest) {
                u.alphaFunc = 0x04;
            }
            PipelineKey k = makeCanonicalPipelineKey(u, u.vtxFlags, true, true, VK_COMPARE_OP_LESS_OR_EQUAL,
                                                    false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                    VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                                    VK_CULL_MODE_BACK_BIT);
            inv.addEntry(u, k, "planet_terrain", false);
        }
    }

    // 6. Particles, Star Bits, and Glows
    for (uint32_t blendMode = 0; blendMode <= 1; blendMode++) {
        ShaderUid u{};
        u.vtxFlags = 0x0051;
        u.numChans = 1;
        u.numTexGens = 1;
        u.numTevStages = 1;
        u.colorCtrl[0] = 0x0001;
        u.texMtxInfo[0] = 5 << 7;
        u.tevColor[0] = 0x080A00;
        u.tevAlpha[0] = 0x080A00;

        VkBlendFactor srcF = VK_BLEND_FACTOR_SRC_ALPHA;
        VkBlendFactor dstF = (blendMode == 0) ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

        PipelineKey k = makeCanonicalPipelineKey(u, u.vtxFlags, true, false, VK_COMPARE_OP_LESS_OR_EQUAL,
                                                true, srcF, dstF, srcF, dstF,
                                                VK_CULL_MODE_NONE);
        inv.addEntry(u, k, "particles_and_starbits", false);
    }
}

}  // namespace gpu
