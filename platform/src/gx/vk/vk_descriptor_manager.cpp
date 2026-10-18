#include "gx/vk/vk_common.h"

namespace gpu {

bool VkRenderer::createDescriptorLayouts() {
    // Set 0: SSBOs & EyeBlock UBO
    // Binding 0: RowsBuf (Vertex)
    // Binding 1: RecsBuf (Vertex)
    // Binding 2: StatesBuf (Vertex | Fragment)
    // Binding 3: VtxRecBuf (Vertex)
    // Binding 4: EyeBlock (Vertex | Fragment)
    std::array<VkDescriptorSetLayoutBinding, 5> sBindings{};
    sBindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    sBindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    sBindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    sBindings[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    sBindings[4] = {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo sLayoutInfo{};
    sLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sLayoutInfo.bindingCount = (uint32_t)sBindings.size();
    sLayoutInfo.pBindings = sBindings.data();

    if (vkCreateDescriptorSetLayout(mDevice, &sLayoutInfo, nullptr, &mStorageDescLayout) != VK_SUCCESS) {
        return false;
    }

    // Set 1: Combined image samplers s_tex0..s_tex7 (Fragment)
    std::array<VkDescriptorSetLayoutBinding, 8> tBindings{};
    for (uint32_t i = 0; i < 8; i++) {
        tBindings[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    }

    VkDescriptorSetLayoutCreateInfo tLayoutInfo{};
    tLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    tLayoutInfo.bindingCount = (uint32_t)tBindings.size();
    tLayoutInfo.pBindings = tBindings.data();

    if (vkCreateDescriptorSetLayout(mDevice, &tLayoutInfo, nullptr, &mTextureDescLayout) != VK_SUCCESS) {
        return false;
    }

    // Push constant range: u_firstOrdinal (4 bytes, Vertex stage)
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(uint32_t);

    VkDescriptorSetLayout layouts[2] = {mStorageDescLayout, mTextureDescLayout};
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 2;
    plci.pSetLayouts = layouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;

    if (vkCreatePipelineLayout(mDevice, &plci, nullptr, &mPipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Descriptor Pool
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 512},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192},
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 2048;

    return vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &mDescriptorPool) == VK_SUCCESS;
}

VkDescriptorSet VkRenderer::getSamplerDescriptorSet(const std::array<VkImageView, 8>& views,
                                                   const std::array<VkSampler, 8>& samplers) {
    SamplerDescKey key{views, samplers};
    auto it = mSamplerDescSets.find(key);
    if (it != mSamplerDescSets.end()) {
        it->second.lastUse = mFrameNumber;
        return it->second.set;
    }

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = mDescriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &mTextureDescLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(mDevice, &ai, &set) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    std::array<VkDescriptorImageInfo, 8> imageInfos{};
    std::array<VkWriteDescriptorSet, 8> writes{};
    for (uint32_t i = 0; i < 8; i++) {
        imageInfos[i].sampler = samplers[i];
        imageInfos[i].imageView = views[i];
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &imageInfos[i];
    }

    vkUpdateDescriptorSets(mDevice, 8, writes.data(), 0, nullptr);
    SamplerSetEntry entry;
    entry.set = set;
    entry.lastUse = mFrameNumber;
    mSamplerDescSets[key] = entry;
    return set;
}

}  // namespace gpu
