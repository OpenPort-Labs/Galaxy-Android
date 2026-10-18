#include "gx/vk/vk_common.h"

namespace gpu {

bool VkRenderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mPhysicalDevice, mSurface, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDevice, mSurface, &formatCount, nullptr);
    if (formatCount == 0) {
        LOGE("No Vulkan surface formats found");
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDevice, mSurface, &formatCount, formats.data());

    VkSurfaceFormatKHR surfaceFormat = formats[0];
    bool foundUnorm = false;
    for (const auto& fmt : formats) {
        if ((fmt.format == VK_FORMAT_R8G8B8A8_UNORM || fmt.format == VK_FORMAT_B8G8R8A8_UNORM) &&
            fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = fmt;
            foundUnorm = true;
            break;
        }
    }
    if (!foundUnorm) {
        for (const auto& fmt : formats) {
            if (fmt.format == VK_FORMAT_R8G8B8A8_UNORM || fmt.format == VK_FORMAT_B8G8R8A8_UNORM) {
                surfaceFormat = fmt;
                foundUnorm = true;
                break;
            }
        }
    }
    mSwapchainFormat = surfaceFormat.format;
    LOGI("Selected Vulkan swapchain format: %d (foundUnorm=%d)", (int)mSwapchainFormat, foundUnorm ? 1 : 0);

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    mSwapchainUsage = usage;

    VkSurfaceTransformFlagBitsKHR preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    if (!(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
        preTransform = caps.currentTransform;
        LOGW("Vulkan surface does not support an identity transform; using 0x%x", (unsigned)preTransform);
    }
    mSwapchainPreTransform = preTransform;

    VkExtent2D swapExtent;
    if (caps.currentExtent.width != 0xFFFFFFFFu && caps.currentExtent.height != 0xFFFFFFFFu) {
        swapExtent = caps.currentExtent;
    } else {
        swapExtent.width = (uint32_t)mWidth;
        swapExtent.height = (uint32_t)mHeight;
    }
    if (caps.minImageExtent.width > 0) swapExtent.width = std::max(swapExtent.width, caps.minImageExtent.width);
    if (caps.minImageExtent.height > 0) swapExtent.height = std::max(swapExtent.height, caps.minImageExtent.height);
    if (caps.maxImageExtent.width > 0) swapExtent.width = std::min(swapExtent.width, caps.maxImageExtent.width);
    if (caps.maxImageExtent.height > 0) swapExtent.height = std::min(swapExtent.height, caps.maxImageExtent.height);
    if (swapExtent.width == 0 || swapExtent.height == 0) {
        LOGE("Vulkan surface reported an unusable extent %ux%u", swapExtent.width, swapExtent.height);
        return false;
    }
    mSwapchainExtent = swapExtent;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = mSurface;
    sci.minImageCount = imageCount;
    sci.imageFormat = surfaceFormat.format;
    sci.imageColorSpace = surfaceFormat.colorSpace;
    sci.imageExtent = swapExtent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = usage;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(mPhysicalDevice, mSurface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(modeCount);
    if (modeCount > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(mPhysicalDevice, mSurface, &modeCount, presentModes.data());
    }

    VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
#if !defined(__ANDROID__)
    for (auto pm : presentModes) {
        if (pm == VK_PRESENT_MODE_MAILBOX_KHR) {
            chosenPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }
#endif

    sci.preTransform = preTransform;
    static const VkCompositeAlphaFlagBitsKHR kAlphaModes[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
    };
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (VkCompositeAlphaFlagBitsKHR mode : kAlphaModes) {
        if (caps.supportedCompositeAlpha & mode) {
            sci.compositeAlpha = mode;
            break;
        }
    }
    sci.presentMode = chosenPresentMode;
    sci.clipped = VK_TRUE;

    LOGI("Creating Vulkan swapchain: extent %ux%u (window %dx%d), transform=0x%x (current=0x%x), presentMode=%d (%s), compositeAlpha=0x%x (supported=0x%x)",
         swapExtent.width, swapExtent.height, mWidth, mHeight, preTransform, caps.currentTransform,
         (int)chosenPresentMode, chosenPresentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX" : "FIFO",
         (unsigned)sci.compositeAlpha, (unsigned)caps.supportedCompositeAlpha);

    if (vkCreateSwapchainKHR(mDevice, &sci, nullptr, &mSwapchain) != VK_SUCCESS) {
        return false;
    }

    uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(mDevice, mSwapchain, &actualImageCount, nullptr);
    mSwapchainImages.resize(actualImageCount);
    vkGetSwapchainImagesKHR(mDevice, mSwapchain, &actualImageCount, mSwapchainImages.data());

    mSwapchainImageViews.resize(actualImageCount);
    for (size_t i = 0; i < actualImageCount; i++) {
        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = mSwapchainImages[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = mSwapchainFormat;
        ivci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(mDevice, &ivci, nullptr, &mSwapchainImageViews[i]) != VK_SUCCESS) {
            return false;
        }
    }

    for (VkSemaphore s : mRenderFinishedSems) {
        if (s != VK_NULL_HANDLE) vkDestroySemaphore(mDevice, s, nullptr);
    }
    mRenderFinishedSems.clear();
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    mRenderFinishedSems.assign(actualImageCount, VK_NULL_HANDLE);
    for (size_t i = 0; i < actualImageCount; i++) {
        if (vkCreateSemaphore(mDevice, &semInfo, nullptr, &mRenderFinishedSems[i]) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool VkRenderer::createSwapchainFramebuffers() {
    for (auto fb : mSwapchainFramebuffers) vkDestroyFramebuffer(mDevice, fb, nullptr);
    mSwapchainFramebuffers.assign(mSwapchainImageViews.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < mSwapchainImageViews.size(); i++) {
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = mPresentRenderPass;
        fci.attachmentCount = 1;
        fci.pAttachments = &mSwapchainImageViews[i];
        fci.width = mSwapchainExtent.width > 0 ? mSwapchainExtent.width : (uint32_t)mWidth;
        fci.height = mSwapchainExtent.height > 0 ? mSwapchainExtent.height : (uint32_t)mHeight;
        fci.layers = 1;
        if (vkCreateFramebuffer(mDevice, &fci, nullptr, &mSwapchainFramebuffers[i]) != VK_SUCCESS) {
            LOGE("Failed to create swapchain framebuffer %zu", i);
            return false;
        }
    }
    return true;
}

void VkRenderer::onWindowResized(ANativeWindow* window) {
    std::lock_guard<std::recursive_mutex> lock(mRenderMutex);
    if (!mInitialized || !window) return;
    int newW = ANativeWindow_getWidth(window);
    int newH = ANativeWindow_getHeight(window);
    if (newW <= 0 || newH <= 0) return;

    mWindow = window;
    mWidth = newW;
    mHeight = newH;

    if (mDevice != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(mDevice);

        for (auto fb : mSwapchainFramebuffers) vkDestroyFramebuffer(mDevice, fb, nullptr);
        mSwapchainFramebuffers.clear();

        for (auto iv : mSwapchainImageViews) vkDestroyImageView(mDevice, iv, nullptr);
        mSwapchainImageViews.clear();

        if (mSwapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
            mSwapchain = VK_NULL_HANDLE;
        }

        const VkFormat oldFormat = mSwapchainFormat;
        if (!createSwapchain()) {
            LOGE("Failed to recreate swapchain during window resize");
            return;
        }

        if (mSwapchainFormat != oldFormat) {
            LOGI("Swapchain format changed (%d -> %d); rebuilding render passes and pipelines",
                 (int)oldFormat, (int)mSwapchainFormat);
            destroyUiPipeline();
            destroyPresentationPipeline();
            for (auto& pair : mPipelines) {
                if (pair.second) vkDestroyPipeline(mDevice, pair.second, nullptr);
            }
            mPipelines.clear();
            if (!createRenderPasses() || !createPresentationPipeline() || !createUiPipeline()) {
                LOGE("Failed to rebuild the swapchain-format dependent state");
                return;
            }
        } else if (!createSwapchainFramebuffers()) {
            LOGE("Failed to recreate swapchain framebuffers");
            return;
        }
    }
    LOGI("Resized Vulkan swapchain to %ux%u (window %dx%d)",
         mSwapchainExtent.width, mSwapchainExtent.height, mWidth, mHeight);
}

}  // namespace gpu
