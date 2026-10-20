#include "gx/vk/vk_common.h"
#include "gx/vk_shader_inventory.h"
#include "gx/vk/vk_pipeline_blend.h"

namespace gpu {

static uint64_t sPlaceholderDraws = 0;

PipelineKey pipelineKeyFor(const Item& it, const ShaderUid& uid) {
    DecodedPipelineKey d{};
    d.uid = uid;
    d.vtxFlags = it.flags;
    d.depthTest = (it.depthTest != 0);
    d.depthWrite = (it.depthTest && it.depthWrite);
    d.depthCompareOp = toCanonicalDepthFunc(it.depthFunc);

    d.blendEnable = (it.blend != 0) || (it.dstAlpha != 0);
    d.srcColorBlendFactor = toCanonicalBlendFactor(it.blend ? it.srcRGB : 1);
    d.dstColorBlendFactor = toCanonicalBlendFactor(it.blend ? it.dstRGB : 0);
    d.colorBlendOp = toCanonicalBlendOp(it.blend ? it.eq : 0x8006);
    d.srcAlphaBlendFactor = toCanonicalBlendFactor(it.dstAlpha ? 0x8003 : (it.blend ? it.srcRGB : 1));
    d.dstAlphaBlendFactor = toCanonicalBlendFactor(it.dstAlpha ? 0 : (it.blend ? it.dstRGB : 0));
    d.alphaBlendOp = toCanonicalBlendOp(it.dstAlpha ? 0x8006 : (it.blend ? it.eq : 0x8006));
    d.colorWriteMask = it.colorMask & 0x0Fu;
    d.blendConstantAlpha = it.dstAlphaValue;

    d.cullMode = toCanonicalCullMode(it.cull != 0, it.cullFace);
    d.topology = toCanonicalTopology(it.prim);
    d.frontFace = CanonicalFrontFace::CounterClockwise;
    return encodePipelineKey(d);
}

VkPipeline VkRenderer::getPipeline(const Item& it, const ShaderUid& uid) {
    const PipelineKey key = pipelineKeyFor(it, uid);
    auto pit = mPipelines.find(key);
    if (pit != mPipelines.end()) return pit->second;

    std::vector<uint32_t> vsSpirv, fsSpirv;
    if (!lookupVulkanShader(uid, vsSpirv, fsSpirv)) {
        ShaderInventory::get().recordRuntimeShaderUidMiss(uid, key, "runtime_uid_miss");
        if (mShaderFailed.count(uid)) {
            return placeholderPipeline(it);
        }
        requestShader(uid, it);
        return placeholderPipeline(it);
    }

    ShaderInventory::get().recordRuntimePipelineKeyMiss(uid, key, "runtime_key_miss");
    return createPipeline(key, vsSpirv, fsSpirv);
}

PrewarmResult VkRenderer::prewarmExactPipeline(const PipelineKey& key, const ShaderUid& uid) {
    if (mDevice == VK_NULL_HANDLE || mEfbRenderPass == VK_NULL_HANDLE) {
        return PrewarmResult::UnsupportedState;
    }

    auto pit = mPipelines.find(key);
    if (pit != mPipelines.end() && pit->second != VK_NULL_HANDLE) {
        return PrewarmResult::Reused;
    }

    std::vector<uint32_t> vsSpirv, fsSpirv;
    if (!lookupVulkanShader(uid, vsSpirv, fsSpirv)) {
        if (mShaderFailed.count(uid)) {
            return PrewarmResult::SpirvUnavailable;
        }
        if (!compileVulkanShader(uid, vsSpirv, fsSpirv)) {
            mShaderFailed.insert(uid);
            return PrewarmResult::SpirvUnavailable;
        }
    }

    VkPipeline pipeline = createPipeline(key, vsSpirv, fsSpirv);
    if (pipeline == VK_NULL_HANDLE) {
        return PrewarmResult::CreationFailed;
    }
    return PrewarmResult::Created;
}

VkPipeline VkRenderer::placeholderPipeline(const Item& it) {
    sPlaceholderDraws++;
    ShaderInventory::get().incrementPlaceholderUses();
    if (sPlaceholderDraws == 1 || sPlaceholderDraws % 500 == 0) {
        LOGW("VkRenderer: %llu draws currently utilizing placeholder fallback pipelines", (unsigned long long)sPlaceholderDraws);
    }

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
    return createPipeline(key, vsSpirv, fsSpirv);
}

VkPipeline VkRenderer::createPipeline(const PipelineKey& key,
                                      const std::vector<uint32_t>& vsSpirv, const std::vector<uint32_t>& fsSpirv) {
    const DecodedPipelineKey dec = decodePipelineKey(key);
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
    if (dec.vtxFlags & VF_POSMTX) addAttr(1, VK_FORMAT_R32_UINT, 4);
    if (dec.vtxFlags & VF_NRM) addAttr(2, VK_FORMAT_R32G32B32_SFLOAT, 12);
    if (dec.vtxFlags & VF_NBT) {
        addAttr(3, VK_FORMAT_R32G32B32_SFLOAT, 12);
        addAttr(4, VK_FORMAT_R32G32B32_SFLOAT, 12);
    }
    if (dec.vtxFlags & VF_CLR0) addAttr(5, VK_FORMAT_R8G8B8A8_UNORM, 4);
    if (dec.vtxFlags & VF_CLR1) addAttr(6, VK_FORMAT_R8G8B8A8_UNORM, 4);
    for (int i = 0; i < 8; i++) {
        if (dec.vtxFlags & (VF_TEX0 << i)) addAttr(7 + i, VK_FORMAT_R32G32_SFLOAT, 8);
    }
    if (dec.vtxFlags & VF_TEXMTX) addAttr(15, VK_FORMAT_R32G32_UINT, 8);

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
    iaci.topology = toVkTopology(dec.topology);

    VkPipelineViewportStateCreateInfo vpsi{};
    vpsi.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpsi.viewportCount = 1;
    vpsi.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rsci{};
    rsci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsci.depthClampEnable = mDeviceFeatures.depthClamp ? VK_TRUE : VK_FALSE;
    rsci.cullMode = toVkCullMode(dec.cullMode);
    rsci.frontFace = toVkFrontFace(dec.frontFace);
    rsci.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msci{};
    msci.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dssci{};
    dssci.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dssci.depthTestEnable = dec.depthTest ? VK_TRUE : VK_FALSE;
    dssci.depthWriteEnable = dec.depthWrite ? VK_TRUE : VK_FALSE;
    dssci.depthCompareOp = toVkCompareOp(dec.depthCompareOp);

    VkColorComponentFlags mask = 0;
    if (dec.colorWriteMask & 1) mask |= VK_COLOR_COMPONENT_R_BIT;
    if (dec.colorWriteMask & 2) mask |= VK_COLOR_COMPONENT_G_BIT;
    if (dec.colorWriteMask & 4) mask |= VK_COLOR_COMPONENT_B_BIT;
    if (dec.colorWriteMask & 8) mask |= VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendAttachmentState cbAttachment{};
    cbAttachment.colorWriteMask = mask;
    cbAttachment.blendEnable = dec.blendEnable ? VK_TRUE : VK_FALSE;
    cbAttachment.srcColorBlendFactor = toVkBlendFactor(dec.srcColorBlendFactor);
    cbAttachment.dstColorBlendFactor = toVkBlendFactor(dec.dstColorBlendFactor);
    cbAttachment.colorBlendOp = toVkBlendOp(dec.colorBlendOp);
    cbAttachment.srcAlphaBlendFactor = toVkBlendFactor(dec.srcAlphaBlendFactor);
    cbAttachment.dstAlphaBlendFactor = toVkBlendFactor(dec.dstAlphaBlendFactor);
    cbAttachment.alphaBlendOp = toVkBlendOp(dec.alphaBlendOp);

    VkPipelineColorBlendStateCreateInfo cbci{};
    cbci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbci.attachmentCount = 1;
    cbci.pAttachments = &cbAttachment;
    cbci.blendConstants[0] = cbci.blendConstants[1] = cbci.blendConstants[2] = 0.0f;
    cbci.blendConstants[3] = dec.blendConstantAlpha;

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
    int64_t t0 = port_host_time_ns();
    VkResult res = vkCreateGraphicsPipelines(mDevice, mPipelineCache, 1, &pci, nullptr, &pipeline);
    if (res != VK_SUCCESS || pipeline == VK_NULL_HANDLE) {
        if (mPipelineCache != VK_NULL_HANDLE) {
            LOGW("VkRenderer: vkCreateGraphicsPipelines failed with cache (res=%d), falling back without cache", (int)res);
            res = vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline);
        }
    }
    port_perf_pipeline_create(port_host_time_ns() - t0);

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
        LOGI("VkRenderer: pipeline built in %.1f ms (GLSL->SPIR-V %.1f ms, driver %.1f ms)", totalMs,
             std::chrono::duration<double, std::milli>(spirvDone - buildStart).count(),
             std::chrono::duration<double, std::milli>(built - spirvDone).count());
    }
    return pipeline;
}

}  // namespace gpu
