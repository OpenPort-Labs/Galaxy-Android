#include "gx/vk/vk_common.h"

namespace gpu {

bool VkRenderer::createRenderPasses() {
    if (mPresentRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(mDevice, mPresentRenderPass, nullptr);
        mPresentRenderPass = VK_NULL_HANDLE;
    }
    if (mEfbRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(mDevice, mEfbRenderPass, nullptr);
        mEfbRenderPass = VK_NULL_HANDLE;
    }
    if (mEfbLoadRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(mDevice, mEfbLoadRenderPass, nullptr);
        mEfbLoadRenderPass = VK_NULL_HANDLE;
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
    efbAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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

    VkSubpassDependency efbDeps[2]{};
    efbDeps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    efbDeps[0].dstSubpass = 0;
    efbDeps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    efbDeps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    efbDeps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    efbDeps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    efbDeps[1].srcSubpass = 0;
    efbDeps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    efbDeps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    efbDeps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    efbDeps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    efbDeps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo efbRpInfo{};
    efbRpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    efbRpInfo.attachmentCount = 2;
    efbRpInfo.pAttachments = efbAttachments;
    efbRpInfo.subpassCount = 1;
    efbRpInfo.pSubpasses = &efbSubpass;
    efbRpInfo.dependencyCount = 2;
    efbRpInfo.pDependencies = efbDeps;

    if (vkCreateRenderPass(mDevice, &efbRpInfo, nullptr, &mEfbRenderPass) != VK_SUCCESS) {
        LOGE("Failed to create Vulkan EFB render pass");
        return false;
    }

    // 3. EFB load render pass (resumes mid-frame after EFB copies)
    VkAttachmentDescription efbLoadAttachments[2] = {efbAttachments[0], efbAttachments[1]};
    efbLoadAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    efbLoadAttachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    efbLoadAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    if (hasStencil) efbLoadAttachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    efbLoadAttachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkRenderPassCreateInfo efbLoadRpInfo = efbRpInfo;
    efbLoadRpInfo.pAttachments = efbLoadAttachments;

    if (vkCreateRenderPass(mDevice, &efbLoadRpInfo, nullptr, &mEfbLoadRenderPass) != VK_SUCCESS) {
        LOGE("Failed to create Vulkan EFB load render pass");
        return false;
    }

    return createSwapchainFramebuffers();
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
    allocInfo.commandBufferCount = 2;

    VkCommandBuffer cmdBufs[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    if (vkAllocateCommandBuffers(mDevice, &allocInfo, cmdBufs) != VK_SUCCESS) {
        return false;
    }
    mCommandBuffer = cmdBufs[0];
    mPresentCommandBuffer = cmdBufs[1];
    return true;
}

bool VkRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateSemaphore(mDevice, &semInfo, nullptr, &mImageAvailableSem) != VK_SUCCESS) return false;
    if (vkCreateSemaphore(mDevice, &semInfo, nullptr, &mRenderToPresentSem) != VK_SUCCESS) return false;
    if (vkCreateFence(mDevice, &fenceInfo, nullptr, &mRenderFence) != VK_SUCCESS) return false;
    if (vkCreateFence(mDevice, &fenceInfo, nullptr, &mPresentFence) != VK_SUCCESS) return false;

    mRenderFenceInFlight = false;
    mPresentFenceInFlight = false;
    return true;
}

}  // namespace gpu
