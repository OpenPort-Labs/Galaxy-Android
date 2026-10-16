#include "vk_renderer.h"

#include <android/log.h>
#include <algorithm>
#include <array>
#include <cstring>

#define TAG "PetariVK"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

namespace gpu {

static VkRenderer sVkRenderer;

VkRenderer& VkRenderer::get() {
    return sVkRenderer;
}

VkRenderer& vkRenderer() {
    return sVkRenderer;
}

bool VkRenderer::init(ANativeWindow* window) {
    std::lock_guard<std::recursive_mutex> lock(mRenderMutex);
    if (mInitialized) return true;
    mWindow = window;
    mWidth = ANativeWindow_getWidth(window);
    mHeight = ANativeWindow_getHeight(window);

    LOGI("Initializing Vulkan 1.0 native renderer (%dx%d)...", mWidth, mHeight);

    if (!createInstance()) {
        LOGE("Failed to create Vulkan 1.0 instance");
        return false;
    }

    if (!createSurface(window)) {
        LOGE("Failed to create Android Vulkan surface");
        return false;
    }

    if (!selectPhysicalDevice()) {
        LOGE("Failed to select Vulkan physical device");
        return false;
    }

    if (!createLogicalDevice()) {
        LOGE("Failed to create Vulkan logical device");
        return false;
    }

    if (!createSwapchain()) {
        LOGE("Failed to create Vulkan swapchain");
        return false;
    }

    if (!createRenderPasses()) {
        LOGE("Failed to create Vulkan render passes");
        return false;
    }

    if (!createCommandPool()) {
        LOGE("Failed to create Vulkan command pool");
        return false;
    }

    if (!createSyncObjects()) {
        LOGE("Failed to create Vulkan sync objects");
        return false;
    }

    if (!createDescriptorLayouts()) {
        LOGE("Failed to create Vulkan descriptor layouts");
        return false;
    }

    if (!createPresentationPipeline()) {
        LOGE("Failed to create Vulkan presentation pipeline");
        return false;
    }

    if (!createUiPipeline()) {
        LOGE("Failed to create Vulkan UI pipeline");
        return false;
    }

    // 1x1 dummy white texture for unbound sampler units
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
        vkCreateImage(mDevice, &ici, nullptr, &mDummyTexture.image);

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(mDevice, mDummyTexture.image, &memReqs);
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReqs.size;
        mai.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(mDevice, &mai, nullptr, &mDummyTexture.memory);
        vkBindImageMemory(mDevice, mDummyTexture.image, mDummyTexture.memory, 0);

        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = mDummyTexture.image;
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        vkCreateImageView(mDevice, &ivci, nullptr, &mDummyTexture.view);

        // Dummy sampler
        VkSamplerCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sci.magFilter = VK_FILTER_LINEAR;
        sci.minFilter = VK_FILTER_LINEAR;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.maxLod = 1.0f;
        vkCreateSampler(mDevice, &sci, nullptr, &mDummySampler);

        // Transition dummy texture to SHADER_READ_ONLY_OPTIMAL
        vkResetCommandBuffer(mCommandBuffer, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(mCommandBuffer, &bi);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = mDummyTexture.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        vkEndCommandBuffer(mCommandBuffer);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &mCommandBuffer;
        vkQueueSubmit(mQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(mQueue);
    }

    mInitialized = true;
    LOGI("Vulkan 1.0 native renderer initialized successfully on %s", mDeviceProperties.deviceName);
    return true;
}

void VkRenderer::shutdown() {
    std::lock_guard<std::recursive_mutex> lock(mRenderMutex);
    if (!mInitialized) return;

    if (mDevice != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(mDevice);

        if (mFlightFence != VK_NULL_HANDLE) vkDestroyFence(mDevice, mFlightFence, nullptr);
        if (mImageAvailableSem != VK_NULL_HANDLE) vkDestroySemaphore(mDevice, mImageAvailableSem, nullptr);
        if (mRenderFinishedSem != VK_NULL_HANDLE) vkDestroySemaphore(mDevice, mRenderFinishedSem, nullptr);

        if (mDummySampler != VK_NULL_HANDLE) vkDestroySampler(mDevice, mDummySampler, nullptr);
        if (mDummyTexture.view != VK_NULL_HANDLE) vkDestroyImageView(mDevice, mDummyTexture.view, nullptr);
        if (mDummyTexture.image != VK_NULL_HANDLE) vkDestroyImage(mDevice, mDummyTexture.image, nullptr);
        if (mDummyTexture.memory != VK_NULL_HANDLE) vkFreeMemory(mDevice, mDummyTexture.memory, nullptr);
        mDummyTexture = {};

        for (auto& pair : mTextures) {
            if (pair.second.view) vkDestroyImageView(mDevice, pair.second.view, nullptr);
            if (pair.second.image) vkDestroyImage(mDevice, pair.second.image, nullptr);
            if (pair.second.memory) vkFreeMemory(mDevice, pair.second.memory, nullptr);
        }
        mTextures.clear();

        for (auto& pair : mSamplers) {
            if (pair.second) vkDestroySampler(mDevice, pair.second, nullptr);
        }
        mSamplers.clear();

        for (auto& pair : mPipelines) {
            if (pair.second) vkDestroyPipeline(mDevice, pair.second, nullptr);
        }
        mPipelines.clear();

        // Presentation resources
        if (mPresentPipeline != VK_NULL_HANDLE) vkDestroyPipeline(mDevice, mPresentPipeline, nullptr);
        if (mPresentPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(mDevice, mPresentPipelineLayout, nullptr);
        if (mPresentDescLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(mDevice, mPresentDescLayout, nullptr);
        if (mPresentSampler != VK_NULL_HANDLE) vkDestroySampler(mDevice, mPresentSampler, nullptr);
        mPresentDescSets.clear();

        // UI overlay resources
        destroyUiPipeline();

        // Frame resources
        for (auto& fr : mFrameRes) {
            if (fr.vbo) vkDestroyBuffer(mDevice, fr.vbo, nullptr);
            if (fr.vboMem) vkFreeMemory(mDevice, fr.vboMem, nullptr);
            if (fr.ibo) vkDestroyBuffer(mDevice, fr.ibo, nullptr);
            if (fr.iboMem) vkFreeMemory(mDevice, fr.iboMem, nullptr);
            if (fr.rowBuf) vkDestroyBuffer(mDevice, fr.rowBuf, nullptr);
            if (fr.rowMem) vkFreeMemory(mDevice, fr.rowMem, nullptr);
            if (fr.recBuf) vkDestroyBuffer(mDevice, fr.recBuf, nullptr);
            if (fr.recMem) vkFreeMemory(mDevice, fr.recMem, nullptr);
            if (fr.stBuf) vkDestroyBuffer(mDevice, fr.stBuf, nullptr);
            if (fr.stMem) vkFreeMemory(mDevice, fr.stMem, nullptr);
            if (fr.vtxRecBuf) vkDestroyBuffer(mDevice, fr.vtxRecBuf, nullptr);
            if (fr.vtxRecMem) vkFreeMemory(mDevice, fr.vtxRecMem, nullptr);
            if (fr.eyeUbo) vkDestroyBuffer(mDevice, fr.eyeUbo, nullptr);
            if (fr.eyeMem) vkFreeMemory(mDevice, fr.eyeMem, nullptr);
            if (fr.uiVbo) vkDestroyBuffer(mDevice, fr.uiVbo, nullptr);
            if (fr.uiVboMem) vkFreeMemory(mDevice, fr.uiVboMem, nullptr);
            fr = {};
        }

        if (mPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
        if (mDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(mDevice, mDescriptorPool, nullptr);
        if (mStorageDescLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(mDevice, mStorageDescLayout, nullptr);
        if (mTextureDescLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(mDevice, mTextureDescLayout, nullptr);

        if (mCommandPool != VK_NULL_HANDLE) vkDestroyCommandPool(mDevice, mCommandPool, nullptr);

        for (auto fb : mSwapchainFramebuffers) vkDestroyFramebuffer(mDevice, fb, nullptr);
        mSwapchainFramebuffers.clear();

        for (auto iv : mSwapchainImageViews) vkDestroyImageView(mDevice, iv, nullptr);
        mSwapchainImageViews.clear();

        if (mEfbRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(mDevice, mEfbRenderPass, nullptr);
        if (mPresentRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(mDevice, mPresentRenderPass, nullptr);

        if (mSwapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
        vkDestroyDevice(mDevice, nullptr);
    }

    if (mInstance != VK_NULL_HANDLE) {
        if (mSurface != VK_NULL_HANDLE) vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
        vkDestroyInstance(mInstance, nullptr);
    }

    mInstance = VK_NULL_HANDLE;
    mDevice = VK_NULL_HANDLE;
    mSurface = VK_NULL_HANDLE;
    mSwapchain = VK_NULL_HANDLE;
    mInitialized = false;
    LOGI("Vulkan 1.0 renderer shut down cleanly.");
}

bool VkRenderer::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "GalaxyAndroid";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "PetariVK";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = 2;
    createInfo.ppEnabledExtensionNames = extensions;

    VkResult res = vkCreateInstance(&createInfo, nullptr, &mInstance);
    return res == VK_SUCCESS;
}

bool VkRenderer::createSurface(ANativeWindow* window) {
    VkAndroidSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    sci.window = window;
    return vkCreateAndroidSurfaceKHR(mInstance, &sci, nullptr, &mSurface) == VK_SUCCESS;
}

bool VkRenderer::selectPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(mInstance, &deviceCount, nullptr);
    if (deviceCount == 0) return false;

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(mInstance, &deviceCount, devices.data());

    for (const auto& dev : devices) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, mSurface, &presentSupport);
            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
                mPhysicalDevice = dev;
                mQueueFamily = i;
                vkGetPhysicalDeviceProperties(dev, &mDeviceProperties);
                vkGetPhysicalDeviceFeatures(dev, &mDeviceFeatures);
                LOGI("Selected Vulkan GPU: %s (Driver version: 0x%x, API: %d.%d)",
                     mDeviceProperties.deviceName, mDeviceProperties.driverVersion,
                     VK_VERSION_MAJOR(mDeviceProperties.apiVersion),
                     VK_VERSION_MINOR(mDeviceProperties.apiVersion));
                return true;
            }
        }
    }
    return false;
}

bool VkRenderer::createLogicalDevice() {
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = mQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = deviceExtensions;

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = mDeviceFeatures.samplerAnisotropy;
    createInfo.pEnabledFeatures = &deviceFeatures;

    if (vkCreateDevice(mPhysicalDevice, &createInfo, nullptr, &mDevice) != VK_SUCCESS) {
        return false;
    }

    vkGetDeviceQueue(mDevice, mQueueFamily, 0, &mQueue);
    return true;
}

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
    for (const auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_R8G8B8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = fmt;
            break;
        }
    }
    mSwapchainFormat = surfaceFormat.format;

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
    }

    VkExtent2D swapExtent;
    if (preTransform == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR || caps.currentExtent.width == 0xFFFFFFFF) {
        swapExtent.width = (uint32_t)mWidth;
        swapExtent.height = (uint32_t)mHeight;
    } else {
        swapExtent = caps.currentExtent;
    }

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
    sci.preTransform = preTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;

    LOGI("Creating Vulkan swapchain: extent %ux%u (window %dx%d), transform=0x%x (current=0x%x)",
         swapExtent.width, swapExtent.height, mWidth, mHeight, preTransform, caps.currentTransform);

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

    return true;
}

bool VkRenderer::createRenderPasses() {
    if (mPresentRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(mDevice, mPresentRenderPass, nullptr);
        mPresentRenderPass = VK_NULL_HANDLE;
    }
    if (mEfbRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(mDevice, mEfbRenderPass, nullptr);
        mEfbRenderPass = VK_NULL_HANDLE;
    }

    if (mDepthFormat == VK_FORMAT_UNDEFINED) {
        mDepthFormat = findSupportedDepthFormat();
    }

    // 1. Present render pass (drawing fullscreen quad to swapchain)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = mSwapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(mDevice, &renderPassInfo, nullptr, &mPresentRenderPass) != VK_SUCCESS) {
        return false;
    }

    // 2. EFB offscreen render pass (Color + Depth/Stencil attachments)
    VkAttachmentDescription efbAttachments[2]{};
    efbAttachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    efbAttachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    efbAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    efbAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    efbAttachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    efbAttachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    efbAttachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    efbAttachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    bool hasStencil = (mDepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                       mDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
                       mDepthFormat == VK_FORMAT_D16_UNORM_S8_UINT);

    efbAttachments[1].format = mDepthFormat;
    efbAttachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    efbAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    efbAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    efbAttachments[1].stencilLoadOp = hasStencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    efbAttachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    efbAttachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    efbAttachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription efbSubpass{};
    efbSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    efbSubpass.colorAttachmentCount = 1;
    efbSubpass.pColorAttachments = &colorRef;
    efbSubpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency efbDep{};
    efbDep.srcSubpass = VK_SUBPASS_EXTERNAL;
    efbDep.dstSubpass = 0;
    efbDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    efbDep.srcAccessMask = 0;
    efbDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    efbDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo efbRpInfo{};
    efbRpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    efbRpInfo.attachmentCount = 2;
    efbRpInfo.pAttachments = efbAttachments;
    efbRpInfo.subpassCount = 1;
    efbRpInfo.pSubpasses = &efbSubpass;
    efbRpInfo.dependencyCount = 1;
    efbRpInfo.pDependencies = &efbDep;

    if (vkCreateRenderPass(mDevice, &efbRpInfo, nullptr, &mEfbRenderPass) != VK_SUCCESS) {
        LOGE("Failed to create Vulkan EFB render pass");
        return false;
    }

    mSwapchainFramebuffers.resize(mSwapchainImageViews.size());
    for (size_t i = 0; i < mSwapchainImageViews.size(); i++) {
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = mPresentRenderPass;
        fci.attachmentCount = 1;
        fci.pAttachments = &mSwapchainImageViews[i];
        fci.width = mWidth;
        fci.height = mHeight;
        fci.layers = 1;
        if (vkCreateFramebuffer(mDevice, &fci, nullptr, &mSwapchainFramebuffers[i]) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool VkRenderer::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = mQueueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(mDevice, &poolInfo, nullptr, &mCommandPool) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = mCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    return vkAllocateCommandBuffers(mDevice, &allocInfo, &mCommandBuffer) == VK_SUCCESS;
}

bool VkRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    return vkCreateSemaphore(mDevice, &semInfo, nullptr, &mImageAvailableSem) == VK_SUCCESS &&
           vkCreateSemaphore(mDevice, &semInfo, nullptr, &mRenderFinishedSem) == VK_SUCCESS &&
           vkCreateFence(mDevice, &fenceInfo, nullptr, &mFlightFence) == VK_SUCCESS;
}

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
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048},
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 512;

    return vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &mDescriptorPool) == VK_SUCCESS;
}

bool VkRenderer::createPresentationPipeline() {
    // Set 0: 1 sampler for EFB color target
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr, &mPresentDescLayout) != VK_SUCCESS) {
        return false;
    }

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &mPresentDescLayout;

    if (vkCreatePipelineLayout(mDevice, &plci, nullptr, &mPresentPipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Sampler for presentation
    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(mDevice, &sci, nullptr, &mPresentSampler) != VK_SUCCESS) {
        return false;
    }

    // Fullscreen quad shaders
    const char* vsSrc = R"(#version 450 core
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
    const char* fsSrc = R"(#version 450 core
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D s_target;
void main() {
    outColor = texture(s_target, v_uv);
}
)";

    std::vector<uint32_t> vsSpirv, fsSpirv;
    std::string err;
    if (!compileShaderToSPIRV(vsSrc, true, vsSpirv, err) ||
        !compileShaderToSPIRV(fsSrc, false, fsSpirv, err)) {
        LOGE("Failed to compile presentation shaders: %s", err.c_str());
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

    VkPipelineVertexInputStateCreateInfo vici{};
    vici.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
    rsci.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rsci.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msci{};
    msci.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cbAttachment{};
    cbAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbAttachment.blendEnable = VK_FALSE;

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
    pci.layout = mPresentPipelineLayout;
    pci.renderPass = mPresentRenderPass;

    VkResult res = vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pci, nullptr, &mPresentPipeline);

    vkDestroyShaderModule(mDevice, vsMod, nullptr);
    vkDestroyShaderModule(mDevice, fsMod, nullptr);

    return res == VK_SUCCESS;
}

bool VkRenderer::createUiPipeline() {
    // 1. Descriptor Set Layout: Binding 0 is font sampler
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr, &mUiDescLayout) != VK_SUCCESS) {
        LOGE("Failed to create UI descriptor set layout");
        return false;
    }

    // 2. Pipeline Layout with push constants for orthographic projection (mat4 = 64 bytes)
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

    // 3. UI Font Sampler
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

    // 4. Decode Font Atlas and upload to VkImage
    int atlasW = 0, atlasH = 0;
    std::vector<uint8_t> coverage;
    mobile_ui::UIOverlay::getFontAtlas(atlasW, atlasH, coverage);

    if (atlasW > 0 && atlasH > 0 && !coverage.empty()) {
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

        if (vkCreateImage(mDevice, &ici, nullptr, &mUiFontTexture.image) == VK_SUCCESS) {
            VkMemoryRequirements memReqs;
            vkGetImageMemoryRequirements(mDevice, mUiFontTexture.image, &memReqs);

            VkMemoryAllocateInfo mai{};
            mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            mai.allocationSize = memReqs.size;
            mai.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(mDevice, &mai, nullptr, &mUiFontTexture.memory) == VK_SUCCESS) {
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

                // Upload via staging buffer
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
            }
        }
    }

    // Allocate and update UI descriptor set
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

    // 5. Compile UI Shaders (GLSL 450 -> SPIR-V 1.0)
    const char* vsSrc = R"(#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aParams;

layout(push_constant) uniform PushConsts {
    mat4 uProj;
};

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec4 vParams;

void main() {
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
    vUv = aUv;
    vColor = aColor;
    vParams = aParams;
}
)";

    const char* fsSrc = R"(#version 450 core
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec4 vParams;

layout(set = 0, binding = 0) uniform sampler2D uFontTex;

layout(location = 0) out vec4 FragColor;

void main() {
    float mode = vParams.x;
    float param = vParams.y;
    float alpha = 1.0;

    if (mode > 0.5 && mode < 1.5) {
        // Mode 1: Circle or Ring (vUv in -1..1)
        float d = length(vUv);
        float fw = fwidth(d);
        if (fw < 0.001) fw = 0.005;
        float outer = smoothstep(1.0, 1.0 - fw, d);
        if (param > 0.0) {
            float inner = smoothstep(param - fw, param, d);
            alpha = outer * inner;
        } else {
            alpha = outer;
        }
    } else if (mode > 1.5 && mode < 2.5) {
        // Mode 2: Font glyph texture: sample red channel
        alpha = texture(uFontTex, vUv).r;
    } else if (mode > 2.5 && mode < 3.5) {
        // Mode 3: Iconic Material 3 Settings Cogwheel
        float d = length(vUv);
        float fw = fwidth(d);
        if (fw < 0.001) fw = 0.005;
        float angle = atan(vUv.y, vUv.x) + param;
        float teeth = cos(8.0 * angle);
        float rMax = 0.74 + 0.22 * smoothstep(-0.25, 0.25, teeth);
        float outer = smoothstep(rMax, rMax - fw * 1.5, d);
        float inner = smoothstep(0.33 - fw * 1.5, 0.33, d);
        alpha = outer * inner;
    } else if (mode > 3.5 && mode < 4.5) {
        // Mode 4: Material 3 Close 'X' icon
        vec2 p = abs(vUv);
        float lineDist = abs(p.x - p.y) * 0.7071;
        float capDist = max(p.x, p.y);
        float fw = fwidth(lineDist);
        if (fw < 0.001) fw = 0.005;
        float line = smoothstep(0.16, 0.16 - fw, lineDist);
        float box = smoothstep(0.65, 0.65 - fw, capDist);
        alpha = line * box;
    } else if (mode > 4.5 && mode < 5.5) {
        // Mode 5: Spin Cyclone / Galaxy Swirl Glyph
        float d = length(vUv);
        float a = atan(vUv.y, vUv.x) + param;
        float aFold = mod(a, 3.14159265);
        float normAngle = clamp((aFold - 0.25) / 2.35, 0.0, 1.0);
        float arcR = mix(0.42, 0.78, normAngle);
        float dArc = abs(d - arcR);
        float thick = mix(0.06, 0.16, normAngle);
        float fw = fwidth(dArc);
        if (fw < 0.001) fw = 0.005;
        float arcAlpha = smoothstep(thick, thick - fw * 1.5, dArc);
        float angleMask = smoothstep(0.12, 0.30, aFold) * smoothstep(2.80, 2.60, aFold);
        alpha = arcAlpha * angleMask;
    } else if (mode > 5.5 && mode < 6.5) {
        // Mode 6: Modern Precision Chevron Arrow
        float ca = cos(param);
        float sa = sin(param);
        vec2 p = vec2(ca * vUv.x - sa * vUv.y, sa * vUv.x + ca * vUv.y);
        vec2 tip = vec2(0.30, 0.0);
        vec2 wing = vec2(-0.30, 0.52);
        vec2 seg = wing - tip;
        vec2 pt = vec2(p.x, abs(p.y)) - tip;
        float t = clamp(dot(pt, seg) / dot(seg, seg), 0.0, 1.0);
        float dLine = length(pt - seg * t);
        float fw = fwidth(dLine);
        if (fw < 0.001) fw = 0.005;
        float strokeW = 0.16;
        alpha = smoothstep(strokeW, strokeW - fw * 1.5, dLine);
    } else if (mode > 6.5 && mode < 7.5) {
        // Mode 7: Modern Minimalist Camera Glyph
        vec2 p = vUv;
        vec2 bBody = vec2(0.48, 0.32);
        vec2 pBody = p - vec2(0.0, 0.06);
        vec2 dBody = abs(pBody) - bBody;
        float distBody = length(max(dBody, 0.0)) + min(max(dBody.x, dBody.y), 0.0) - 0.10;

        vec2 bTop = vec2(0.18, 0.08);
        vec2 pTop = p - vec2(-0.16, -0.32);
        vec2 dTop = abs(pTop) - bTop;
        float distTop = length(max(dTop, 0.0)) + min(max(dTop.x, dTop.y), 0.0) - 0.05;

        float bodyUnion = min(distBody, distTop);
        float fw = fwidth(length(p));
        if (fw < 0.001) fw = 0.005;
        float fillBody = smoothstep(0.0, -fw * 1.5, bodyUnion);

        float dLens = length(p - vec2(0.0, 0.08));
        float lensInner = smoothstep(0.16 - fw, 0.16, dLens);
        float lensOuter = smoothstep(0.24, 0.24 - fw, dLens);
        float lensRing = lensInner * lensOuter;
        alpha = fillBody + lensRing;
    } else {
        // Mode 0: Rounded rectangle with exact aspect ratio
        if (param > 0.0) {
            vec2 p = abs(vUv);
            float aspect = vParams.z;
            vec2 b = (vParams.w > 0.5) ? vec2(1.0 - param, aspect - param) : vec2(aspect - param, 1.0 - param);
            vec2 d = p - b;
            float dist = length(max(d, 0.0)) - param;
            float fw = fwidth(dist);
            if (fw < 0.001) fw = 0.005;
            alpha = smoothstep(0.0, -fw, dist);
        }
    }

    if (alpha <= 0.002) discard;
    FragColor = vec4(vColor.rgb, vColor.a * alpha);
}
)";

    std::vector<uint32_t> vsSpirv, fsSpirv;
    std::string err;
    if (!compileShaderToSPIRV(vsSrc, true, vsSpirv, err) ||
        !compileShaderToSPIRV(fsSrc, false, fsSpirv, err)) {
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

    // Vertex input format matching mobile_ui::UIVertex
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

    // Blending: SRC_ALPHA, ONE_MINUS_SRC_ALPHA
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

    VkResult res = vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pci, nullptr, &mUiPipeline);

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

void VkRenderer::renderUiOverlay(int width, int height) {
    if (mUiPipeline == VK_NULL_HANDLE || mUiDescSet == VK_NULL_HANDLE) return;

    std::vector<mobile_ui::UIVertex> verts;
    mobile_ui::UIOverlay::get().generateVertices(width, height, verts);
    if (verts.empty()) return;

    FrameResource& fr = mFrameRes[mFrameIndex];
    uploadDynamicBuffer(fr.uiVbo, fr.uiVboMem, fr.uiVboCap,
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        verts.data(), verts.size() * sizeof(mobile_ui::UIVertex));

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = (float)width;
    vp.height = (float)height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(mCommandBuffer, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {(uint32_t)width, (uint32_t)height};
    vkCmdSetScissor(mCommandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mUiPipeline);
    vkCmdBindDescriptorSets(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mUiPipelineLayout, 0, 1, &mUiDescSet, 0, nullptr);

    float proj[16] = {
        2.0f / (float)width,  0.0f,                 0.0f, 0.0f,
        0.0f,                 2.0f / (float)height, 0.0f, 0.0f,
        0.0f,                 0.0f,                 1.0f, 0.0f,
        -1.0f,                -1.0f,                0.0f, 1.0f
    };
    vkCmdPushConstants(mCommandBuffer, mUiPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(proj), proj);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(mCommandBuffer, 0, 1, &fr.uiVbo, &offset);

    vkCmdDraw(mCommandBuffer, (uint32_t)verts.size(), 1, 0, 0);
}

VkFormat VkRenderer::findSupportedDepthFormat() {
    const VkFormat depthFormats[] = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D16_UNORM
    };

    for (VkFormat format : depthFormats) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, format, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            LOGI("Selected Vulkan Depth/Stencil Format: %d", format);
            return format;
        }
    }
    LOGE("No supported depth/stencil format found on this device!");
    return VK_FORMAT_UNDEFINED;
}

uint32_t VkRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return ~0u;
}

bool VkRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                             VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(mDevice, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(mDevice, buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, properties);
    if (allocInfo.memoryTypeIndex == ~0u) {
        vkDestroyBuffer(mDevice, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }

    if (vkAllocateMemory(mDevice, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        vkDestroyBuffer(mDevice, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(mDevice, buffer, bufferMemory, 0);
    return true;
}

void VkRenderer::uploadDynamicBuffer(VkBuffer& buf, VkDeviceMemory& mem, size_t& currentCap,
                                    VkBufferUsageFlags usage, const void* data, size_t bytes) {
    if (bytes == 0) return;
    if (bytes > currentCap || buf == VK_NULL_HANDLE) {
        if (buf != VK_NULL_HANDLE) vkDestroyBuffer(mDevice, buf, nullptr);
        if (mem != VK_NULL_HANDLE) vkFreeMemory(mDevice, mem, nullptr);

        currentCap = bytes + bytes / 4 + 4096;
        createBuffer((VkDeviceSize)currentCap, usage,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     buf, mem);
    }

    void* mapped = nullptr;
    if (vkMapMemory(mDevice, mem, 0, bytes, 0, &mapped) == VK_SUCCESS) {
        memcpy(mapped, data, bytes);
        vkUnmapMemory(mDevice, mem);
    }
}

VkShaderModule VkRenderer::createShaderModule(const uint32_t* code, size_t sizeBytes) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = sizeBytes;
    createInfo.pCode = code;

    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(mDevice, &createInfo, nullptr, &module);
    return module;
}

VkTarget VkRenderer::createTarget(int w, int h) {
    VkTarget t{};
    t.width = w;
    t.height = h;

    if (mDepthFormat == VK_FORMAT_UNDEFINED) {
        mDepthFormat = findSupportedDepthFormat();
        if (mDepthFormat == VK_FORMAT_UNDEFINED) {
            LOGE("Cannot create VkTarget: no supported depth format");
            return {};
        }
    }

    // 1. Color attachment image (RGBA8)
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.extent.width = (uint32_t)w;
    ici.extent.height = (uint32_t)h;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(mDevice, &ici, nullptr, &t.colorImage) != VK_SUCCESS) {
        destroyTarget(t);
        return {};
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(mDevice, t.colorImage, &memReqs);

    uint32_t colorMemType = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (colorMemType == ~0u) {
        LOGE("Failed to find suitable device memory type for target color image");
        destroyTarget(t);
        return {};
    }

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = memReqs.size;
    mai.memoryTypeIndex = colorMemType;
    if (vkAllocateMemory(mDevice, &mai, nullptr, &t.colorMemory) != VK_SUCCESS ||
        vkBindImageMemory(mDevice, t.colorImage, t.colorMemory, 0) != VK_SUCCESS) {
        destroyTarget(t);
        return {};
    }

    VkImageViewCreateInfo ivci{};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = t.colorImage;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(mDevice, &ivci, nullptr, &t.colorView) != VK_SUCCESS) {
        destroyTarget(t);
        return {};
    }

    // 2. Depth/stencil attachment image (dynamic supported depth format)
    VkImageCreateInfo dici = ici;
    dici.format = mDepthFormat;
    dici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    if (vkCreateImage(mDevice, &dici, nullptr, &t.depthImage) != VK_SUCCESS) {
        destroyTarget(t);
        return {};
    }

    vkGetImageMemoryRequirements(mDevice, t.depthImage, &memReqs);
    uint32_t depthMemType = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (depthMemType == ~0u) {
        LOGE("Failed to find suitable device memory type for target depth image");
        destroyTarget(t);
        return {};
    }

    mai.allocationSize = memReqs.size;
    mai.memoryTypeIndex = depthMemType;
    if (vkAllocateMemory(mDevice, &mai, nullptr, &t.depthMemory) != VK_SUCCESS ||
        vkBindImageMemory(mDevice, t.depthImage, t.depthMemory, 0) != VK_SUCCESS) {
        destroyTarget(t);
        return {};
    }

    VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (mDepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        mDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
        mDepthFormat == VK_FORMAT_D16_UNORM_S8_UINT) {
        depthAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    VkImageViewCreateInfo divci{};
    divci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    divci.image = t.depthImage;
    divci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    divci.format = mDepthFormat;
    divci.subresourceRange.aspectMask = depthAspect;
    divci.subresourceRange.levelCount = 1;
    divci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(mDevice, &divci, nullptr, &t.depthView) != VK_SUCCESS) {
        destroyTarget(t);
        return {};
    }

    // 3. Target Framebuffer bound to mEfbRenderPass
    VkImageView attachments[2] = {t.colorView, t.depthView};
    VkFramebufferCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = mEfbRenderPass;
    fci.attachmentCount = 2;
    fci.pAttachments = attachments;
    fci.width = (uint32_t)w;
    fci.height = (uint32_t)h;
    fci.layers = 1;
    if (vkCreateFramebuffer(mDevice, &fci, nullptr, &t.framebuffer) != VK_SUCCESS) {
        destroyTarget(t);
        return {};
    }

    return t;
}

void VkRenderer::destroyTarget(VkTarget& target) {
    if (mDevice == VK_NULL_HANDLE) return;
    if (target.framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(mDevice, target.framebuffer, nullptr);
    if (target.depthView != VK_NULL_HANDLE) vkDestroyImageView(mDevice, target.depthView, nullptr);
    if (target.depthImage != VK_NULL_HANDLE) vkDestroyImage(mDevice, target.depthImage, nullptr);
    if (target.depthMemory != VK_NULL_HANDLE) vkFreeMemory(mDevice, target.depthMemory, nullptr);
    if (target.colorView != VK_NULL_HANDLE) vkDestroyImageView(mDevice, target.colorView, nullptr);
    if (target.colorImage != VK_NULL_HANDLE) vkDestroyImage(mDevice, target.colorImage, nullptr);
    if (target.colorMemory != VK_NULL_HANDLE) vkFreeMemory(mDevice, target.colorMemory, nullptr);
    target = {};
}

void VkRenderer::onWindowResized(ANativeWindow* window) {
    std::lock_guard<std::recursive_mutex> lock(mRenderMutex);
    if (!mInitialized) return;
    mWindow = window;
    mWidth = ANativeWindow_getWidth(window);
    mHeight = ANativeWindow_getHeight(window);

    vkDeviceWaitIdle(mDevice);

    for (auto fb : mSwapchainFramebuffers) vkDestroyFramebuffer(mDevice, fb, nullptr);
    mSwapchainFramebuffers.clear();

    for (auto iv : mSwapchainImageViews) vkDestroyImageView(mDevice, iv, nullptr);
    mSwapchainImageViews.clear();

    if (mSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
        mSwapchain = VK_NULL_HANDLE;
    }

    if (!createSwapchain()) {
        LOGE("Failed to recreate swapchain during window resize");
        return;
    }
    if (!createRenderPasses()) {
        LOGE("Failed to recreate render passes during window resize");
        return;
    }
    LOGI("Resized Vulkan swapchain to %dx%d", mWidth, mHeight);
}

VkSampler VkRenderer::getSampler(uint32_t mode0, uint32_t mode1, uint32_t levels) {
    uint64_t key = ((uint64_t)(mode0 & 0xFFFFFF) << 32) | ((mode1 & 0xFFFF) << 8) | (levels > 1 ? 1 : 0);
    auto it = mSamplers.find(key);
    if (it != mSamplers.end()) return it->second;

    static const VkSamplerAddressMode wrap[4] = {
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT
    };

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.addressModeU = wrap[mode0 & 3];
    sci.addressModeV = wrap[(mode0 >> 2) & 3];
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.magFilter = ((mode0 >> 4) & 1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

    uint32_t minf = (mode0 >> 5) & 7;
    bool mips = levels > 1;
    switch (minf) {
    case 0:
        sci.minFilter = VK_FILTER_NEAREST;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case 1:
        sci.minFilter = VK_FILTER_NEAREST;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case 2:
        sci.minFilter = VK_FILTER_NEAREST;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
    case 5:
        sci.minFilter = VK_FILTER_LINEAR;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case 6:
        sci.minFilter = VK_FILTER_LINEAR;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
    default:
        sci.minFilter = VK_FILTER_LINEAR;
        sci.mipmapMode = mips ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    }

    sci.minLod = (mode1 & 0xFF) / 16.0f;
    sci.maxLod = ((mode1 >> 8) & 0xFF) / 16.0f;
    if (sci.maxLod < sci.minLod) sci.maxLod = sci.minLod + (float)levels;

    VkSampler s = VK_NULL_HANDLE;
    vkCreateSampler(mDevice, &sci, nullptr, &s);
    mSamplers[key] = s;
    return s;
}

void VkRenderer::uploadTextures(const std::vector<std::shared_ptr<TexImage>>& textures) {
    for (const auto& img : textures) {
        if (!img || img->rgba.empty()) continue;
        auto it = mTextures.find(img.get());
        if (it != mTextures.end() && it->second.version == img->version) {
            continue;
        }

        if (it != mTextures.end()) {
            if (it->second.view) vkDestroyImageView(mDevice, it->second.view, nullptr);
            if (it->second.image) vkDestroyImage(mDevice, it->second.image, nullptr);
            if (it->second.memory) vkFreeMemory(mDevice, it->second.memory, nullptr);
            mTextures.erase(it);
        }

        VkTexture vt{};
        vt.width = img->width;
        vt.height = img->height;
        vt.levels = img->levels ? img->levels : 1;
        vt.version = img->version;

        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.extent = {vt.width, vt.height, 1};
        ici.mipLevels = vt.levels;
        ici.arrayLayers = 1;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(mDevice, &ici, nullptr, &vt.image) != VK_SUCCESS) continue;

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(mDevice, vt.image, &memReqs);

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReqs.size;
        mai.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(mDevice, &mai, nullptr, &vt.memory) != VK_SUCCESS) {
            vkDestroyImage(mDevice, vt.image, nullptr);
            continue;
        }
        vkBindImageMemory(mDevice, vt.image, vt.memory, 0);

        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = vt.image;
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = vt.levels;
        ivci.subresourceRange.layerCount = 1;
        vkCreateImageView(mDevice, &ivci, nullptr, &vt.view);

        // Upload texture pixels via host-visible staging buffer
        size_t totalBytes = img->rgba.size() * sizeof(uint32_t);
        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (createBuffer(totalBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuf, stagingMem)) {
            void* mapped = nullptr;
            if (vkMapMemory(mDevice, stagingMem, 0, totalBytes, 0, &mapped) == VK_SUCCESS) {
                memcpy(mapped, img->rgba.data(), totalBytes);
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
            barrierToDst.image = vt.image;
            barrierToDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrierToDst.subresourceRange.levelCount = vt.levels;
            barrierToDst.subresourceRange.layerCount = 1;
            barrierToDst.srcAccessMask = 0;
            barrierToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToDst);

            std::vector<VkBufferImageCopy> regions(vt.levels);
            VkDeviceSize offset = 0;
            uint32_t w = vt.width, h = vt.height;
            for (uint32_t l = 0; l < vt.levels; l++) {
                regions[l].bufferOffset = offset;
                regions[l].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                regions[l].imageSubresource.mipLevel = l;
                regions[l].imageSubresource.layerCount = 1;
                regions[l].imageExtent = {w, h, 1};
                offset += (VkDeviceSize)w * h * 4;
                w = w > 1 ? w / 2 : 1;
                h = h > 1 ? h / 2 : 1;
            }

            vkCmdCopyBufferToImage(mCommandBuffer, stagingBuf, vt.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   (uint32_t)regions.size(), regions.data());

            VkImageMemoryBarrier barrierToShader = barrierToDst;
            barrierToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrierToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrierToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrierToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToShader);

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

        mTextures[img.get()] = vt;
    }
}

VkPipeline VkRenderer::getPipeline(const Item& it, const ShaderUid& uid) {
    PipelineKey key{};
    key.uidIndex = it.uid;
    key.flags = it.flags;
    key.depthState = (it.depthTest ? 1 : 0) | ((it.depthTest && it.depthWrite ? 1 : 0) << 1) | ((it.depthFunc & 15) << 4);
    key.blendState = (it.blend ? 1 : 0) | ((it.srcRGB & 31) << 1) | ((it.dstRGB & 31) << 6) | ((it.eq & 7) << 11) |
                     ((it.colorMask & 15) << 16) | ((it.dstAlpha ? 1 : 0) << 20);
    key.rasterState = (it.cull ? 1 : 0) | ((it.cullFace & 3) << 1) | ((it.prim & 7) << 4);

    auto pit = mPipelines.find(key);
    if (pit != mPipelines.end()) return pit->second;

    std::vector<uint32_t> vsSpirv, fsSpirv;
    if (!compileVulkanShader(uid, vsSpirv, fsSpirv)) {
        LOGE("Failed to generate and compile Vulkan SPIR-V for shader uid %u", it.uid);
        return VK_NULL_HANDLE;
    }

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

    // Vertex input layout derived from it.flags
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

    addAttr(0, VK_FORMAT_R32G32B32_SFLOAT, 12); // a_pos
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
    rsci.polygonMode = VK_POLYGON_MODE_FILL;
    rsci.cullMode = it.cull ? ((it.cullFace == 0x0404) ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT) : VK_CULL_MODE_NONE;
    rsci.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rsci.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msci{};
    msci.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth & Stencil state
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

    // Color blend state
    auto mapBlendFactor = [](uint32_t glFactor) -> VkBlendFactor {
        switch (glFactor) {
        case 0: return VK_BLEND_FACTOR_ZERO;
        case 1: return VK_BLEND_FACTOR_ONE;
        case 0x0300: return VK_BLEND_FACTOR_SRC_COLOR;
        case 0x0301: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 0x0302: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 0x0303: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x0304: return VK_BLEND_FACTOR_DST_ALPHA;
        case 0x0305: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 0x0306: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 0x0307: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 0x8003: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        default: return VK_BLEND_FACTOR_ONE;
        }
    };

    VkPipelineColorBlendAttachmentState cbAttachment{};
    cbAttachment.colorWriteMask = (it.colorMask & 1 ? VK_COLOR_COMPONENT_R_BIT : 0) |
                                  (it.colorMask & 2 ? VK_COLOR_COMPONENT_G_BIT : 0) |
                                  (it.colorMask & 4 ? VK_COLOR_COMPONENT_B_BIT : 0) |
                                  (it.colorMask & 8 ? VK_COLOR_COMPONENT_A_BIT : 0);
    cbAttachment.blendEnable = (it.blend || it.dstAlpha) ? VK_TRUE : VK_FALSE;
    cbAttachment.srcColorBlendFactor = mapBlendFactor(it.srcRGB);
    cbAttachment.dstColorBlendFactor = mapBlendFactor(it.dstRGB);
    cbAttachment.colorBlendOp = (it.eq == 0x8007) ? VK_BLEND_OP_SUBTRACT :
                                ((it.eq == 0x8008) ? VK_BLEND_OP_REVERSE_SUBTRACT : VK_BLEND_OP_ADD);
    cbAttachment.srcAlphaBlendFactor = mapBlendFactor(it.dstAlpha ? 0x8003 : it.srcRGB);
    cbAttachment.dstAlphaBlendFactor = mapBlendFactor(it.dstAlpha ? 0 : it.dstRGB);
    cbAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

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
    vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline);

    vkDestroyShaderModule(mDevice, vsMod, nullptr);
    vkDestroyShaderModule(mDevice, fsMod, nullptr);

    mPipelines[key] = pipeline;
    return pipeline;
}

VkDescriptorSet VkRenderer::getSamplerDescriptorSet(const std::array<VkImageView, 8>& views,
                                                   const std::array<VkSampler, 8>& samplers) {
    SamplerDescKey key{views, samplers};
    auto it = mSamplerDescSets.find(key);
    if (it != mSamplerDescSets.end()) return it->second;

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
    mSamplerDescSets[key] = set;
    return set;
}

bool VkRenderer::update() {
    std::lock_guard<std::recursive_mutex> lock(mRenderMutex);
    auto latest = frameQueue().latest();
    if (latest && (!mCurrentFrame || latest->number != mFrameNumber)) {
        mFrameNumber = latest->number;
        mCurrentFrame = std::move(latest);
        if (!mPrepared) {
            mPrepared = std::make_unique<Prepared>();
        }
        mBuilder.build(mCurrentFrame, *mPrepared);
        return true;
    }
    return false;
}

bool VkRenderer::render(VkTarget& target, const EyeView* eye, VkTarget* hud, HudMode hudMode) {
    std::lock_guard<std::recursive_mutex> lock(mRenderMutex);
    if (!mInitialized || target.framebuffer == VK_NULL_HANDLE) {
        return false;
    }

    if (!mCurrentFrame) {
        update();
    }
    if (!mCurrentFrame || !mPrepared) {
        return false;
    }

    const Frame& f = *mCurrentFrame;
    Prepared& p = *mPrepared;
    const std::vector<Item>& items = p.items;
    if (items.empty()) {
        return false;
    }

    vkWaitForFences(mDevice, 1, &mFlightFence, VK_TRUE, UINT64_MAX);

    // Rotate frame resources
    mFrameIndex = (mFrameIndex + 1) % 2;
    FrameResource& fr = mFrameRes[mFrameIndex];

    // Upload geometry & storage buffers
    uploadDynamicBuffer(fr.vbo, fr.vboMem, fr.vboCap, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, f.verts.data(), f.verts.size() * sizeof(uint32_t));
    uploadDynamicBuffer(fr.ibo, fr.iboMem, fr.iboCap, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, p.indices.data(), p.indices.size() * sizeof(uint32_t));
    uploadDynamicBuffer(fr.rowBuf, fr.rowMem, fr.rowCap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, p.rows.data(), p.rows.size() * sizeof(float));
    uploadDynamicBuffer(fr.recBuf, fr.recMem, fr.recCap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, p.recs.data(), p.recs.size() * sizeof(uint32_t));
    uploadDynamicBuffer(fr.stBuf, fr.stMem, fr.stCap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, p.states.data(), p.states.size() * sizeof(float));
    uploadDynamicBuffer(fr.vtxRecBuf, fr.vtxRecMem, fr.vtxRecCap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, p.vtxRec.data(), p.vtxRec.size() * sizeof(uint32_t));

    // Upload EyeBlock UBO
    EyeBlock eb{};
    bool vrEye = eye && !eye->flatStereo;
    if (vrEye) {
        for (int i = 0; i < 16; i++) {
            eb.vrView[i / 4][i % 4] = eye->view[i];
            eb.vrProj[i / 4][i % 4] = eye->proj[i];
        }
        eb.vrFlags[0] = 1;
        memcpy(eb.vrFocus, eye->focus, sizeof(eb.vrFocus));
        memcpy(eb.vrCut, eye->cut, sizeof(eb.vrCut));
        memcpy(eb.vrEyePos, eye->eyePos, sizeof(eb.vrEyePos));
    } else if (eye) {
        eb.vrFlags[0] = 2;
        memcpy(eb.vrStereo, eye->stereo, sizeof(eb.vrStereo));
        eb.vrStereo2[0] = eye->pointer[0];
        eb.vrStereo2[1] = eye->pointer[1];
    }
    uploadDynamicBuffer(fr.eyeUbo, fr.eyeMem, fr.eyeCap, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &eb, sizeof(eb));

    // Allocate / update Set 0 for this frame resource
    if (fr.set0 == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = mDescriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &mStorageDescLayout;
        vkAllocateDescriptorSets(mDevice, &ai, &fr.set0);
    }

    VkDescriptorBufferInfo bInfo[5]{};
    bInfo[0] = {fr.rowBuf, 0, VK_WHOLE_SIZE};
    bInfo[1] = {fr.recBuf, 0, VK_WHOLE_SIZE};
    bInfo[2] = {fr.stBuf, 0, VK_WHOLE_SIZE};
    bInfo[3] = {fr.vtxRecBuf, 0, VK_WHOLE_SIZE};
    bInfo[4] = {fr.eyeUbo, 0, sizeof(EyeBlock)};

    VkWriteDescriptorSet writes[5]{};
    for (uint32_t i = 0; i < 4; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = fr.set0;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &bInfo[i];
    }
    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = fr.set0;
    writes[4].dstBinding = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[4].pBufferInfo = &bInfo[4];

    vkUpdateDescriptorSets(mDevice, 5, writes, 0, nullptr);

    // Upload new / updated textures
    uploadTextures(f.textures);

    vkResetCommandBuffer(mCommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(mCommandBuffer, &beginInfo) != VK_SUCCESS) {
        return false;
    }

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = mEfbRenderPass;
    rpInfo.framebuffer = target.framebuffer;
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = {(uint32_t)target.width, (uint32_t)target.height};

    VkClearValue clearValues[2]{};
    clearValues[0].color = {{
        (p.clearAR & 255) / 255.0f,
        ((p.clearGB >> 8) & 255) / 255.0f,
        (p.clearGB & 255) / 255.0f,
        ((p.clearAR >> 8) & 255) / 255.0f
    }};
    clearValues[1].depthStencil = {p.clearZ / 16777215.0f, 0};
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(mCommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind set 0 (SSBOs & UBO)
    vkCmdBindDescriptorSets(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0, 1, &fr.set0, 0, nullptr);

    float fx = (float)target.width / (float)p.nativeW;
    float fy = (float)target.height / (float)p.nativeH;

    bool inHudSection = false;
    for (const Item& it : items) {
        if (it.type == IT_MARKER) {
            inHudSection = (it.marker == MARK_HUD_BEGIN);
            continue;
        }
        if (inHudSection && hudMode == HudMode::Skip) {
            continue;
        }
        if (it.type != IT_DRAW || !it.idxCount) {
            continue;
        }

        // Viewport & Scissor
        int vx, vy, vw, vh;
        int sx, sy, sw, sh;
        float targetAspect = (float)target.width / (float)target.height;
        const float k169 = 16.0f / 9.0f;
        if (it.hudSafe && targetAspect > k169 + 0.01f) {
            float hudW = (float)target.height * k169;
            float hudOfsX = ((float)target.width - hudW) * 0.5f;
            float hudFx = hudW / (float)p.nativeW;
            vx = (int)lroundf(hudOfsX + it.vp[0] * hudFx);
            vw = (int)lroundf(it.vp[2] * hudFx);
            sx = (int)lroundf(hudOfsX + it.sc[0] * hudFx);
            int sr = (int)lroundf(hudOfsX + it.sc[2] * hudFx);
            sw = sr - sx;
        } else {
            vx = (int)lroundf(it.vp[0] * fx);
            vw = (int)lroundf(it.vp[2] * fx);
            sx = (int)lroundf(it.sc[0] * fx);
            int sr = (int)lroundf(it.sc[2] * fx);
            sw = sr - sx;
        }
        vh = (int)lroundf(it.vp[3] * fy);
        vy = target.height - (int)lroundf(it.vp[1] * fy) - vh;

        int st = (int)lroundf(it.sc[1] * fy);
        int sb = (int)lroundf(it.sc[3] * fy);
        sy = target.height - sb;
        sh = sb - st;

        if (vw <= 0) vw = 1;
        if (vh <= 0) vh = 1;
        if (sw <= 0) sw = 1;
        if (sh <= 0) sh = 1;
        if (sx < 0) { sw += sx; sx = 0; }
        if (sy < 0) { sh += sy; sy = 0; }
        if (sx + sw > target.width) sw = target.width - sx;
        if (sy + sh > target.height) sh = target.height - sy;
        if (sw <= 0 || sh <= 0) continue;

        VkViewport viewport{};
        viewport.x = (float)vx;
        viewport.y = (float)vy;
        viewport.width = (float)vw;
        viewport.height = (float)vh;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(mCommandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {sx, sy};
        scissor.extent = {(uint32_t)sw, (uint32_t)sh};
        vkCmdSetScissor(mCommandBuffer, 0, 1, &scissor);

        // Pipeline
        VkPipeline pipeline = getPipeline(it, p.uids[it.uid]);
        if (pipeline == VK_NULL_HANDLE) continue;
        vkCmdBindPipeline(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        // Push constants
        vkCmdPushConstants(mCommandBuffer, mPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &it.firstOrdinal);

        // Textures & Samplers for Set 1
        std::array<VkImageView, 8> views;
        std::array<VkSampler, 8> samplers;
        for (int u = 0; u < 8; u++) {
            uint32_t tr = it.tex[u];
            if ((it.texMask & (1u << u)) && tr != ~0u && !(tr & 0x80000000u) && tr < f.textures.size()) {
                auto tit = mTextures.find(f.textures[tr].get());
                if (tit != mTextures.end() && tit->second.view) {
                    views[u] = tit->second.view;
                    samplers[u] = getSampler(it.texMode0[u], it.texMode1[u], f.textures[tr]->levels);
                } else {
                    views[u] = mDummyTexture.view;
                    samplers[u] = mDummySampler;
                }
            } else {
                views[u] = mDummyTexture.view;
                samplers[u] = mDummySampler;
            }
        }
        VkDescriptorSet set1 = getSamplerDescriptorSet(views, samplers);
        if (set1 != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 1, 1, &set1, 0, nullptr);
        }

        // Bind vertex & index buffers
        VkDeviceSize vOffset = (VkDeviceSize)it.firstWord * 4;
        vkCmdBindVertexBuffers(mCommandBuffer, 0, 1, &fr.vbo, &vOffset);
        vkCmdBindIndexBuffer(mCommandBuffer, fr.ibo, (VkDeviceSize)it.idxOffset * 4, VK_INDEX_TYPE_UINT32);

        // Draw indexed
        vkCmdDrawIndexed(mCommandBuffer, it.idxCount, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(mCommandBuffer);

    VkResult endRes = vkEndCommandBuffer(mCommandBuffer);
    if (endRes != VK_SUCCESS) {
        LOGE("Failed to end Vulkan render command buffer: %d", endRes);
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &mCommandBuffer;

    vkResetFences(mDevice, 1, &mFlightFence);
    VkResult subRes = vkQueueSubmit(mQueue, 1, &submitInfo, mFlightFence);
    if (subRes != VK_SUCCESS) {
        LOGE("Failed to submit Vulkan render commands: %d", subRes);
        return false;
    }

    return true;
}

void VkRenderer::present(const VkTarget& target, int vpX, int vpY, int vpW, int vpH) {
    std::lock_guard<std::recursive_mutex> lock(mRenderMutex);
    if (!mInitialized || mSwapchain == VK_NULL_HANDLE || target.colorView == VK_NULL_HANDLE) return;

    vkWaitForFences(mDevice, 1, &mFlightFence, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult acquireRes = vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX, mImageAvailableSem, VK_NULL_HANDLE, &imageIndex);
    if (acquireRes == VK_ERROR_OUT_OF_DATE_KHR || acquireRes == VK_SUBOPTIMAL_KHR) {
        onWindowResized(mWindow);
        return;
    }
    if (acquireRes != VK_SUCCESS) {
        LOGE("vkAcquireNextImageKHR failed: %d", acquireRes);
        return;
    }

    // Get / allocate descriptor set for target color view
    VkDescriptorSet presentSet = VK_NULL_HANDLE;
    auto sit = mPresentDescSets.find(target.colorView);
    if (sit != mPresentDescSets.end()) {
        presentSet = sit->second;
    } else {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = mDescriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &mPresentDescLayout;
        if (vkAllocateDescriptorSets(mDevice, &ai, &presentSet) == VK_SUCCESS) {
            VkDescriptorImageInfo ii{};
            ii.sampler = mPresentSampler;
            ii.imageView = target.colorView;
            ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = presentSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &ii;
            vkUpdateDescriptorSets(mDevice, 1, &write, 0, nullptr);
            mPresentDescSets[target.colorView] = presentSet;
        }
    }

    vkResetCommandBuffer(mCommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(mCommandBuffer, &beginInfo) != VK_SUCCESS) {
        LOGE("vkBeginCommandBuffer failed in present");
        return;
    }

    // Presentation render pass clears to black and draws target quad
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = mPresentRenderPass;
    rpInfo.framebuffer = mSwapchainFramebuffers[imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = {(uint32_t)mWidth, (uint32_t)mHeight};

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(mCommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (hasFrame() && presentSet != VK_NULL_HANDLE && mPresentPipeline != VK_NULL_HANDLE) {
        VkViewport vp{};
        vp.x = (float)vpX;
        vp.y = (float)vpY;
        vp.width = (float)vpW;
        vp.height = (float)vpH;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(mCommandBuffer, 0, 1, &vp);

        VkRect2D scissor{};
        scissor.offset = {vpX, vpY};
        scissor.extent = {(uint32_t)vpW, (uint32_t)vpH};
        vkCmdSetScissor(mCommandBuffer, 0, 1, &scissor);

        vkCmdBindPipeline(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPresentPipeline);
        vkCmdBindDescriptorSets(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPresentPipelineLayout, 0, 1, &presentSet, 0, nullptr);

        // Draw procedural fullscreen quad (3 vertices)
        vkCmdDraw(mCommandBuffer, 3, 1, 0, 0);
    }

    // Render on-screen controls HUD & Settings overlay directly in the swapchain render pass
    renderUiOverlay(mWidth, mHeight);

    vkCmdEndRenderPass(mCommandBuffer);

    VkResult endRes = vkEndCommandBuffer(mCommandBuffer);
    if (endRes != VK_SUCCESS) {
        LOGE("vkEndCommandBuffer failed: %d", endRes);
        return;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = {mImageAvailableSem};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &mCommandBuffer;
    VkSemaphore signalSemaphores[] = {mRenderFinishedSem};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkResetFences(mDevice, 1, &mFlightFence);
    VkResult submitRes = vkQueueSubmit(mQueue, 1, &submitInfo, mFlightFence);
    if (submitRes != VK_SUCCESS) {
        LOGE("vkQueueSubmit failed in present: %d", submitRes);
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapchains[] = {mSwapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentRes = vkQueuePresentKHR(mQueue, &presentInfo);
    if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR) {
        onWindowResized(mWindow);
    } else if (presentRes != VK_SUCCESS) {
        LOGE("vkQueuePresentKHR failed: %d", presentRes);
    }
}

}  // namespace gpu
