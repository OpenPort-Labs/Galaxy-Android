#include "gx/vk/vk_common.h"

namespace gpu {

void VkRenderer::present(const VkTarget& target, int vpX, int vpY, int vpW, int vpH) {
    std::lock_guard<std::recursive_mutex> lock(mRenderMutex);
    if (!mInitialized || mFatal || mSwapchain == VK_NULL_HANDLE || target.colorView == VK_NULL_HANDLE || !target.hasValidContent) return;

    if (mPresentFenceInFlight) {
        VkResult r = vkWaitForFences(mDevice, 1, &mPresentFence, VK_TRUE, 2000000000ULL);
        mPresentFenceInFlight = false;
        if (r != VK_SUCCESS) {
            markFatal("present", "vkWaitForFences on mPresentFence", (int)r);
            return;
        }
    }

    uint32_t imageIndex = 0;
    VkResult acquireRes = vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX, mImageAvailableSem, VK_NULL_HANDLE, &imageIndex);
    if (acquireRes == VK_ERROR_OUT_OF_DATE_KHR) {
        onWindowResized(mWindow);
        return;
    } else if (acquireRes != VK_SUCCESS && acquireRes != VK_SUBOPTIMAL_KHR) {
        markFatal("present", "vkAcquireNextImageKHR", (int)acquireRes);
        return;
    }

    VkDescriptorSet presentSet = VK_NULL_HANDLE;
    auto sit = mPresentDescSets.find(target.colorView);
    if (sit != mPresentDescSets.end()) {
        presentSet = sit->second;
    } else {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = mStaticDescPool;
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

    vkResetCommandBuffer(mPresentCommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult beginRes = vkBeginCommandBuffer(mPresentCommandBuffer, &beginInfo);
    if (beginRes != VK_SUCCESS) {
        markFatal("present", "vkBeginCommandBuffer", (int)beginRes);
        return;
    }

    if (imageIndex >= mSwapchainFramebuffers.size() || mSwapchainFramebuffers[imageIndex] == VK_NULL_HANDLE) {
        markFatal("present", "swapchain image without framebuffer", (int)imageIndex);
        return;
    }

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = mPresentRenderPass;
    rpInfo.framebuffer = mSwapchainFramebuffers[imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = {mSwapchainExtent.width > 0 ? mSwapchainExtent.width : (uint32_t)mWidth,
                               mSwapchainExtent.height > 0 ? mSwapchainExtent.height : (uint32_t)mHeight};

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(mPresentCommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    const int extW = (int)(mSwapchainExtent.width > 0 ? mSwapchainExtent.width : (uint32_t)mWidth);
    const int extH = (int)(mSwapchainExtent.height > 0 ? mSwapchainExtent.height : (uint32_t)mHeight);
    int qx = vpX, qy = vpY, qw = vpW, qh = vpH;
    if (qx < 0) { qw += qx; qx = 0; }
    if (qy < 0) { qh += qy; qy = 0; }
    if (qw > extW - qx) qw = extW - qx;
    if (qh > extH - qy) qh = extH - qy;

    if (presentSet != VK_NULL_HANDLE && mPresentPipeline != VK_NULL_HANDLE && qw > 0 && qh > 0) {
        VkViewport vp{};
        vp.x = (float)qx;
        vp.y = (float)qy;
        vp.width = (float)qw;
        vp.height = (float)qh;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(mPresentCommandBuffer, 0, 1, &vp);

        VkRect2D scissor{};
        scissor.offset = {qx, qy};
        scissor.extent = {(uint32_t)qw, (uint32_t)qh};
        vkCmdSetScissor(mPresentCommandBuffer, 0, 1, &scissor);

        VkPipeline chosenPipe = mPresentPipeline;
        if (port_is_post_processing_enabled() && mPresentPipelinePostProcess != VK_NULL_HANDLE) {
            chosenPipe = mPresentPipelinePostProcess;
        }
        vkCmdBindPipeline(mPresentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, chosenPipe);
        vkCmdBindDescriptorSets(mPresentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPresentPipelineLayout, 0, 1, &presentSet, 0, nullptr);
        vkCmdDraw(mPresentCommandBuffer, 3, 1, 0, 0);
    }

    renderUiOverlay(mWidth, mHeight, mPresentCommandBuffer);

    vkCmdEndRenderPass(mPresentCommandBuffer);

    VkResult endRes = vkEndCommandBuffer(mPresentCommandBuffer);
    if (endRes != VK_SUCCESS) {
        markFatal("present", "vkEndCommandBuffer", (int)endRes);
        return;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[2];
    VkPipelineStageFlags waitStages[2];
    uint32_t waitCount = 0;

    waitSemaphores[waitCount] = mImageAvailableSem;
    waitStages[waitCount] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    waitCount++;

    if (mRenderSignaledSem) {
        waitSemaphores[waitCount] = mRenderToPresentSem;
        waitStages[waitCount] = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        waitCount++;
        mRenderSignaledSem = false;
    }

    submitInfo.waitSemaphoreCount = waitCount;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &mPresentCommandBuffer;
    VkSemaphore presentWaitSem = (imageIndex < mRenderFinishedSems.size()) ? mRenderFinishedSems[imageIndex] : VK_NULL_HANDLE;
    VkSemaphore signalSemaphores[] = {presentWaitSem};
    if (presentWaitSem == VK_NULL_HANDLE) {
        markFatal("present", "no render-finished semaphore for the acquired image", 0);
        return;
    }
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkResetFences(mDevice, 1, &mPresentFence);
    VkResult submitRes = vkQueueSubmit(mQueue, 1, &submitInfo, mPresentFence);
    if (submitRes != VK_SUCCESS) {
        markFatal("present", "vkQueueSubmit", (int)submitRes);
        return;
    }

    mPresentFenceInFlight = true;

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapchains[] = {mSwapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentRes = vkQueuePresentKHR(mQueue, &presentInfo);
    if (presentRes == VK_ERROR_OUT_OF_DATE_KHR) {
        onWindowResized(mWindow);
    } else if (presentRes == VK_SUBOPTIMAL_KHR) {
        int w = ANativeWindow_getWidth(mWindow);
        int h = ANativeWindow_getHeight(mWindow);
        if (w > 0 && h > 0 && (w != mWidth || h != mHeight)) {
            onWindowResized(mWindow);
        }
    } else if (presentRes != VK_SUCCESS) {
        markFatal("present", "vkQueuePresentKHR", (int)presentRes);
    }
}

}  // namespace gpu
