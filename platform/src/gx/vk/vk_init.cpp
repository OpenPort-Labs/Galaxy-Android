#include "gx/vk/vk_common.h"

namespace gpu {

bool VkRenderer::init(ANativeWindow* window) {
    std::lock_guard<std::recursive_mutex> lock(mRenderMutex);
    if (mInitialized) return true;
    mWindow = window;
    mWidth = ANativeWindow_getWidth(window);
    mHeight = ANativeWindow_getHeight(window);

    LOGI("Initializing Vulkan 1.0 native renderer (%dx%d)...", mWidth, mHeight);
    startShaderWorker();

    if (!createInstance()) {
        LOGE("Failed to create Vulkan 1.0 instance");
        shutdown();
        return false;
    }

    if (!createSurface(window)) {
        LOGE("Failed to create Android Vulkan surface");
        shutdown();
        return false;
    }

    if (!selectPhysicalDevice()) {
        LOGE("Failed to select Vulkan physical device");
        shutdown();
        return false;
    }

    if (!createLogicalDevice()) {
        LOGE("Failed to create Vulkan logical device");
        shutdown();
        return false;
    }

    initPipelineCache();

    if (!createSwapchain()) {
        LOGE("Failed to create Vulkan swapchain");
        shutdown();
        return false;
    }

    if (!createRenderPasses()) {
        LOGE("Failed to create Vulkan render passes");
        shutdown();
        return false;
    }

    if (!createCommandPool()) {
        LOGE("Failed to create Vulkan command pool");
        shutdown();
        return false;
    }

    if (!createSyncObjects()) {
        LOGE("Failed to create Vulkan sync objects");
        shutdown();
        return false;
    }

    if (!createDescriptorLayouts()) {
        LOGE("Failed to create Vulkan descriptor layouts");
        shutdown();
        return false;
    }

    if (!createPresentationPipeline()) {
        LOGE("Failed to create Vulkan presentation pipeline");
        shutdown();
        return false;
    }

    {
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.extent = {1, 1, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(mDevice, &ici, nullptr, &mDummyTexture.image) != VK_SUCCESS) {
            LOGE("Failed to create dummy texture image");
            shutdown();
            return false;
        }

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(mDevice, mDummyTexture.image, &memReqs);
        uint32_t memType = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memType == ~0u) {
            memType = findMemoryType(memReqs.memoryTypeBits, 0);
        }
        if (memType == ~0u) {
            LOGE("Failed to find memory type for dummy texture");
            shutdown();
            return false;
        }

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReqs.size;
        mai.memoryTypeIndex = memType;
        if (vkAllocateMemory(mDevice, &mai, nullptr, &mDummyTexture.memory) != VK_SUCCESS) {
            LOGE("Failed to allocate memory for dummy texture");
            shutdown();
            return false;
        }

        if (vkBindImageMemory(mDevice, mDummyTexture.image, mDummyTexture.memory, 0) != VK_SUCCESS) {
            LOGE("Failed to bind memory for dummy texture");
            shutdown();
            return false;
        }

        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = mDummyTexture.image;
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(mDevice, &ivci, nullptr, &mDummyTexture.view) != VK_SUCCESS) {
            LOGE("Failed to create dummy image view");
            shutdown();
            return false;
        }

        VkSamplerCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sci.magFilter = VK_FILTER_LINEAR;
        sci.minFilter = VK_FILTER_LINEAR;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.maxLod = 1.0f;
        if (vkCreateSampler(mDevice, &sci, nullptr, &mDummySampler) != VK_SUCCESS) {
            LOGE("Failed to create dummy sampler");
            shutdown();
            return false;
        }

        vkResetCommandBuffer(mCommandBuffer, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(mCommandBuffer, &bi) != VK_SUCCESS) {
            LOGE("Failed to begin command buffer for dummy texture setup");
            shutdown();
            return false;
        }

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = mDummyTexture.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkClearColorValue dummyColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkImageSubresourceRange dummyRange{};
        dummyRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        dummyRange.levelCount = 1;
        dummyRange.layerCount = 1;
        vkCmdClearColorImage(mCommandBuffer, mDummyTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &dummyColor, 1, &dummyRange);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        if (vkEndCommandBuffer(mCommandBuffer) != VK_SUCCESS) {
            LOGE("Failed to end command buffer for dummy texture setup");
            shutdown();
            return false;
        }

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &mCommandBuffer;
        if (vkQueueSubmit(mQueue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS ||
            vkQueueWaitIdle(mQueue) != VK_SUCCESS) {
            LOGE("Failed to submit dummy texture transition");
            shutdown();
            return false;
        }
    }

    if (!createUiPipeline()) {
        LOGE("Failed to create Vulkan UI pipeline");
        shutdown();
        return false;
    }

    mFatal = false;
    mInitialized = true;
    LOGI("Vulkan 1.0 native renderer initialized successfully on %s", mDeviceProperties.deviceName);
    return true;
}

}  // namespace gpu
