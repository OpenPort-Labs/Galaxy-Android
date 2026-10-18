#include "gx/vk/vk_common.h"
#include "gx/vk/vk_ui_shaders.h"

namespace gpu {

bool VkRenderer::createUiPipeline() {
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr, &mUiDescLayout) != VK_SUCCESS) {
        LOGE("Failed to create UI descriptor set layout");
        return false;
    }

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(float) * 16;

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &mUiDescLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;

    if (vkCreatePipelineLayout(mDevice, &plci, nullptr, &mUiPipelineLayout) != VK_SUCCESS) {
        LOGE("Failed to create UI pipeline layout");
        return false;
    }

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(mDevice, &sci, nullptr, &mUiFontSampler) != VK_SUCCESS) {
        LOGE("Failed to create UI font sampler");
        return false;
    }

    uploadUiFontAtlas();

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = mDescriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &mUiDescLayout;
    if (vkAllocateDescriptorSets(mDevice, &ai, &mUiDescSet) == VK_SUCCESS) {
        VkDescriptorImageInfo di{};
        di.sampler = mUiFontSampler;
        di.imageView = (mUiFontTexture.view != VK_NULL_HANDLE) ? mUiFontTexture.view : mDummyTexture.view;
        di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = mUiDescSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &di;

        vkUpdateDescriptorSets(mDevice, 1, &write, 0, nullptr);
    }

    std::vector<uint32_t> vsSpirv, fsSpirv;
    std::string err;
    if (!compileShaderToSPIRV(kUiVsSrc, true, vsSpirv, err) ||
        !compileShaderToSPIRV(kUiFsSrc, false, fsSpirv, err)) {
        LOGE("Failed to compile UI shaders: %s", err.c_str());
        return false;
    }

    VkShaderModule vsMod = createShaderModule(vsSpirv.data(), vsSpirv.size() * 4);
    VkShaderModule fsMod = createShaderModule(fsSpirv.data(), fsSpirv.size() * 4);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsMod;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindDesc{};
    bindDesc.binding = 0;
    bindDesc.stride = sizeof(mobile_ui::UIVertex);
    bindDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[4]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(mobile_ui::UIVertex, x)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(mobile_ui::UIVertex, u)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(mobile_ui::UIVertex, r)};
    attrs[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(mobile_ui::UIVertex, mode)};

    VkPipelineVertexInputStateCreateInfo vici{};
    vici.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vici.vertexBindingDescriptionCount = 1;
    vici.pVertexBindingDescriptions = &bindDesc;
    vici.vertexAttributeDescriptionCount = 4;
    vici.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo iaci{};
    iaci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iaci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vpsi{};
    vpsi.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpsi.viewportCount = 1;
    vpsi.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rsci{};
    rsci.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsci.polygonMode = VK_POLYGON_MODE_FILL;
    rsci.cullMode = VK_CULL_MODE_NONE;
    rsci.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rsci.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msci{};
    msci.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cbAttachment{};
    cbAttachment.blendEnable = VK_TRUE;
    cbAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cbAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    cbAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cbAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    cbAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cbci{};
    cbci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbci.attachmentCount = 1;
    cbci.pAttachments = &cbAttachment;

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
    pci.pColorBlendState = &cbci;
    pci.pDynamicState = &dsci;
    pci.layout = mUiPipelineLayout;
    pci.renderPass = mPresentRenderPass;

    VkResult res = vkCreateGraphicsPipelines(mDevice, mPipelineCache, 1, &pci, nullptr, &mUiPipeline);
    if (res != VK_SUCCESS || mUiPipeline == VK_NULL_HANDLE) {
        if (mPipelineCache != VK_NULL_HANDLE) {
            res = vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pci, nullptr, &mUiPipeline);
        }
    }

    vkDestroyShaderModule(mDevice, vsMod, nullptr);
    vkDestroyShaderModule(mDevice, fsMod, nullptr);

    return res == VK_SUCCESS;
}

void VkRenderer::destroyUiPipeline() {
    if (mUiPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(mDevice, mUiPipeline, nullptr); mUiPipeline = VK_NULL_HANDLE; }
    if (mUiPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(mDevice, mUiPipelineLayout, nullptr); mUiPipelineLayout = VK_NULL_HANDLE; }
    if (mUiDescLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(mDevice, mUiDescLayout, nullptr); mUiDescLayout = VK_NULL_HANDLE; }
    if (mUiFontSampler != VK_NULL_HANDLE) { vkDestroySampler(mDevice, mUiFontSampler, nullptr); mUiFontSampler = VK_NULL_HANDLE; }
    if (mUiFontTexture.view != VK_NULL_HANDLE) { vkDestroyImageView(mDevice, mUiFontTexture.view, nullptr); mUiFontTexture.view = VK_NULL_HANDLE; }
    if (mUiFontTexture.image != VK_NULL_HANDLE) { vkDestroyImage(mDevice, mUiFontTexture.image, nullptr); mUiFontTexture.image = VK_NULL_HANDLE; }
    if (mUiFontTexture.memory != VK_NULL_HANDLE) { vkFreeMemory(mDevice, mUiFontTexture.memory, nullptr); mUiFontTexture.memory = VK_NULL_HANDLE; }
    mUiDescSet = VK_NULL_HANDLE;
}

}  // namespace gpu
