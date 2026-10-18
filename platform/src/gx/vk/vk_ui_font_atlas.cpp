#include "gx/vk/vk_common.h"

namespace gpu {

bool VkRenderer::uploadUiFontAtlas() {
    int atlasW = 0, atlasH = 0;
    std::vector<uint8_t> coverage;
    mobile_ui::UIOverlay::getFontAtlas(atlasW, atlasH, coverage);

    if (atlasW <= 0 || atlasH <= 0 || coverage.empty()) {
        return false;
    }

    mUiFontTexture.width = atlasW;
    mUiFontTexture.height = atlasH;
    mUiFontTexture.levels = 1;

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.extent = {(uint32_t)atlasW, (uint32_t)atlasH, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.format = VK_FORMAT_R8_UNORM;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(mDevice, &ici, nullptr, &mUiFontTexture.image) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(mDevice, mUiFontTexture.image, &memReqs);

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = memReqs.size;
    uint32_t fontMemType = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    mai.memoryTypeIndex = fontMemType;
    if (fontMemType == ~0u) {
        LOGE("UI font atlas: no suitable device memory type");
        return false;
    }
    if (vkAllocateMemory(mDevice, &mai, nullptr, &mUiFontTexture.memory) != VK_SUCCESS) {
        return false;
    }
    vkBindImageMemory(mDevice, mUiFontTexture.image, mUiFontTexture.memory, 0);

    VkImageViewCreateInfo ivci{};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = mUiFontTexture.image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = VK_FORMAT_R8_UNORM;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.layerCount = 1;
    vkCreateImageView(mDevice, &ivci, nullptr, &mUiFontTexture.view);

    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    size_t uploadBytes = coverage.size();
    if (createBuffer(uploadBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuf, stagingMem)) {
        void* mapped = nullptr;
        if (vkMapMemory(mDevice, stagingMem, 0, uploadBytes, 0, &mapped) == VK_SUCCESS) {
            memcpy(mapped, coverage.data(), uploadBytes);
            vkUnmapMemory(mDevice, stagingMem);
        }

        vkResetCommandBuffer(mCommandBuffer, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(mCommandBuffer, &bi);

        VkImageMemoryBarrier barrierToDst{};
        barrierToDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrierToDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrierToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrierToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierToDst.image = mUiFontTexture.image;
        barrierToDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrierToDst.subresourceRange.levelCount = 1;
        barrierToDst.subresourceRange.layerCount = 1;
        barrierToDst.srcAccessMask = 0;
        barrierToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToDst);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {(uint32_t)atlasW, (uint32_t)atlasH, 1};

        vkCmdCopyBufferToImage(mCommandBuffer, stagingBuf, mUiFontTexture.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier barrierToRead = barrierToDst;
        barrierToRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrierToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrierToRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrierToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToRead);

        vkEndCommandBuffer(mCommandBuffer);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &mCommandBuffer;
        vkQueueSubmit(mQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(mQueue);

        vkDestroyBuffer(mDevice, stagingBuf, nullptr);
        vkFreeMemory(mDevice, stagingMem, nullptr);
    }
    return true;
}

}  // namespace gpu
