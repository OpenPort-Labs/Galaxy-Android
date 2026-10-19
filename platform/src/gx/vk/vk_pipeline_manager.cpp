#include "gx/vk/vk_common.h"

namespace gpu {

static VkBlendFactor vkBlendFactor(uint32_t glFactor) {
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
    default:
        LOGW("VkRenderer: unknown blend factor 0x%x, defaulting to VK_BLEND_FACTOR_ONE", glFactor);
        return VK_BLEND_FACTOR_ONE;
    }
}

static VkBlendOp vkBlendOp(uint32_t glEq) {
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

static DrawBlendState blendStateFor(const Item& it) {
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

PipelineKey pipelineKeyFor(const Item& it, const ShaderUid& uid) {
    const DrawBlendState blend = blendStateFor(it);
    PipelineKey key{};
    key.uid = uid;
    key.flags = it.flags;
    key.depthState = (it.depthTest ? 1 : 0) | ((it.depthTest && it.depthWrite ? 1 : 0) << 1) | ((it.depthFunc & 15) << 4);
    key.blendState = (blend.on ? 1u : 0u) | ((uint32_t)blend.srcColor << 1) | ((uint32_t)blend.dstColor << 6) |
                     ((uint32_t)blend.srcAlpha << 11) | ((uint32_t)blend.dstAlpha << 16);
    key.blendState2 = (uint32_t)blend.colorOp | ((uint32_t)blend.alphaOp << 2) | ((it.colorMask & 15) << 4) |
                      (blend.constantAlpha << 8);
    key.rasterState = (it.cull ? 1 : 0) | ((it.cullFace & 0xF) << 1) | ((it.prim & 7) << 5);
    return key;
}

VkPipeline VkRenderer::getPipeline(const Item& it, const ShaderUid& uid) {
    const PipelineKey key = pipelineKeyFor(it, uid);
    auto pit = mPipelines.find(key);
    if (pit != mPipelines.end()) return pit->second;

    std::vector<uint32_t> vsSpirv, fsSpirv;
    if (!lookupVulkanShader(uid, vsSpirv, fsSpirv)) {
        if (mShaderFailed.count(uid)) {
            return placeholderPipeline(it);
        }
        requestShader(uid, it);
        return placeholderPipeline(it);
    }
    return createPipeline(it, key, uid, vsSpirv, fsSpirv);
}

VkPipeline VkRenderer::placeholderPipeline(const Item& it) {
    ShaderUid stub{};
    stub.vtxFlags = it.flags;
    const PipelineKey key = pipelineKeyFor(it, stub);
    auto pit = mPipelines.find(key);
    if (pit != mPipelines.end()) return pit->second;

    std::vector<uint32_t> vsSpirv, fsSpirv;
    if (!lookupVulkanShader(stub, vsSpirv, fsSpirv) &&
        !compileVulkanShader(stub, vsSpirv, fsSpirv)) {
        return VK_NULL_HANDLE;
    }
    return createPipeline(it, key, stub, vsSpirv, fsSpirv);
}

VkPipeline VkRenderer::createPipeline(const Item& it, const PipelineKey& key, const ShaderUid& uid,
                                      const std::vector<uint32_t>& vsSpirv, const std::vector<uint32_t>& fsSpirv) {
    const DrawBlendState blend = blendStateFor(it);
    auto buildStart = std::chrono::steady_clock::now();
    auto spirvDone = buildStart;

    VkShaderModule vsMod = createShaderModule(vsSpirv.data(), vsSpirv.size() * 4);
    VkShaderModule fsMod = createShaderModule(fsSpirv.data(), fsSpirv.size() * 4);
    if (!vsMod || !fsMod) {
        if (vsMod) vkDestroyShaderModule(mDevice, vsMod, nullptr);
        if (fsMod) vkDestroyShaderModule(mDevice, fsMod, nullptr);
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsMod;
    stages[1].pName = "main";

    std::vector<VkVertexInputAttributeDescription> attrs;
    uint32_t off = 0;
    auto addAttr = [&](uint32_t loc, VkFormat fmt, uint32_t bytes) {
        VkVertexInputAttributeDescription a{};
        a.location = loc;
        a.binding = 0;
        a.format = fmt;
        a.offset = off;
        attrs.push_back(a);
        off += bytes;
    };

    addAttr(0, VK_FORMAT_R32G32B32_SFLOAT, 12);
    if (it.flags & VF_POSMTX) addAttr(1, VK_FORMAT_R32_UINT, 4);
    if (it.flags & VF_NRM) addAttr(2, VK_FORMAT_R32G32B32_SFLOAT, 12);
    if (it.flags & VF_NBT) {
        addAttr(3, VK_FORMAT_R32G32B32_SFLOAT, 12);
        addAttr(4, VK_FORMAT_R32G32B32_SFLOAT, 12);
    }
    if (it.flags & VF_CLR0) addAttr(5, VK_FORMAT_R8G8B8A8_UNORM, 4);
    if (it.flags & VF_CLR1) addAttr(6, VK_FORMAT_R8G8B8A8_UNORM, 4);
    for (int i = 0; i < 8; i++) {
        if (it.flags & (VF_TEX0 << i)) addAttr(7 + i, VK_FORMAT_R32G32_SFLOAT, 8);
    }
    if (it.flags & VF_TEXMTX) addAttr(15, VK_FORMAT_R32G32_UINT, 8);

    VkVertexInputBindingDescription bindDesc{};
    bindDesc.binding = 0;
    bindDesc.stride = off;
    bindDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkPipelineVertexInputStateCreateInfo vici{};
    vici.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vici.vertexBindingDescriptionCount = 1;
    vici.pVertexBindingDescriptions = &bindDesc;
    vici.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vici.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo iaci{};
    iaci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iaci.topology = (it.prim == 1) ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST :
                    ((it.prim == 0) ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    VkPipelineViewportStateCreateInfo vpsi{};
    vpsi.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpsi.viewportCount = 1;
    vpsi.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rsci{};
    rsci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsci.depthClampEnable = VK_FALSE;
    if (!it.cull) {
        rsci.cullMode = VK_CULL_MODE_NONE;
    } else if (it.cullFace == 0x0408) {
        rsci.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
    } else if (it.cullFace == 0x0404) {
        rsci.cullMode = VK_CULL_MODE_FRONT_BIT;
    } else {
        rsci.cullMode = VK_CULL_MODE_BACK_BIT;
    }
    rsci.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rsci.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msci{};
    msci.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dssci{};
    dssci.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dssci.depthTestEnable = it.depthTest ? VK_TRUE : VK_FALSE;
    dssci.depthWriteEnable = (it.depthTest && it.depthWrite) ? VK_TRUE : VK_FALSE;

    switch (it.depthFunc) {
    case 0x0200: dssci.depthCompareOp = VK_COMPARE_OP_NEVER; break;
    case 0x0201: dssci.depthCompareOp = VK_COMPARE_OP_LESS; break;
    case 0x0202: dssci.depthCompareOp = VK_COMPARE_OP_EQUAL; break;
    case 0x0203: dssci.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; break;
    case 0x0204: dssci.depthCompareOp = VK_COMPARE_OP_GREATER; break;
    case 0x0205: dssci.depthCompareOp = VK_COMPARE_OP_NOT_EQUAL; break;
    case 0x0206: dssci.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
    default: dssci.depthCompareOp = VK_COMPARE_OP_ALWAYS; break;
    }

    VkPipelineColorBlendAttachmentState cbAttachment{};
    cbAttachment.colorWriteMask = (it.colorMask & 1 ? VK_COLOR_COMPONENT_R_BIT : 0) |
                                  (it.colorMask & 2 ? VK_COLOR_COMPONENT_G_BIT : 0) |
                                  (it.colorMask & 4 ? VK_COLOR_COMPONENT_B_BIT : 0) |
                                  (it.colorMask & 8 ? VK_COLOR_COMPONENT_A_BIT : 0);
    cbAttachment.blendEnable = blend.on ? VK_TRUE : VK_FALSE;
    cbAttachment.srcColorBlendFactor = blend.srcColor;
    cbAttachment.dstColorBlendFactor = blend.dstColor;
    cbAttachment.colorBlendOp = blend.colorOp;
    cbAttachment.srcAlphaBlendFactor = blend.srcAlpha;
    cbAttachment.dstAlphaBlendFactor = blend.dstAlpha;
    cbAttachment.alphaBlendOp = blend.alphaOp;

    VkPipelineColorBlendStateCreateInfo cbci{};
    cbci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbci.attachmentCount = 1;
    cbci.pAttachments = &cbAttachment;
    cbci.blendConstants[0] = cbci.blendConstants[1] = cbci.blendConstants[2] = 0.0f;
    cbci.blendConstants[3] = it.dstAlphaValue;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dsci{};
    dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dsci.dynamicStateCount = 2;
    dsci.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vici;
    pci.pInputAssemblyState = &iaci;
    pci.pViewportState = &vpsi;
    pci.pRasterizationState = &rsci;
    pci.pMultisampleState = &msci;
    pci.pDepthStencilState = &dssci;
    pci.pColorBlendState = &cbci;
    pci.pDynamicState = &dsci;
    pci.layout = mPipelineLayout;
    pci.renderPass = mEfbRenderPass;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult res = vkCreateGraphicsPipelines(mDevice, mPipelineCache, 1, &pci, nullptr, &pipeline);
    if (res != VK_SUCCESS || pipeline == VK_NULL_HANDLE) {
        if (mPipelineCache != VK_NULL_HANDLE) {
            LOGW("VkRenderer: vkCreateGraphicsPipelines failed with cache (res=%d), falling back without cache", (int)res);
            res = vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline);
        }
    }

    vkDestroyShaderModule(mDevice, vsMod, nullptr);
    vkDestroyShaderModule(mDevice, fsMod, nullptr);

    if (res != VK_SUCCESS || pipeline == VK_NULL_HANDLE) {
        LOGE("VkRenderer: vkCreateGraphicsPipelines failed: VkResult=%d", res);
        return VK_NULL_HANDLE;
    }

    mPipelines.emplace(key, pipeline);
    auto built = std::chrono::steady_clock::now();
    const double totalMs = std::chrono::duration<double, std::milli>(built - buildStart).count();
    if (totalMs >= 10.0) {
        LOGI("VkRenderer: pipeline uid=%u built in %.1f ms (GLSL->SPIR-V %.1f ms, driver %.1f ms)", it.uid, totalMs,
             std::chrono::duration<double, std::milli>(spirvDone - buildStart).count(),
             std::chrono::duration<double, std::milli>(built - spirvDone).count());
    }
    return pipeline;
}

}  // namespace gpu
