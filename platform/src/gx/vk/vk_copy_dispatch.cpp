#include "gx/vk/vk_common.h"

namespace gpu {

void VkRenderer::executeCopyItem(VkCommandBuffer cmd, const Item& it, const Prepared& p,
                                 VkTarget& target, float fx, float fy, bool& inRenderPass,
                                 const VkRenderPassBeginInfo& rpInfo, FrameResource& fr) {
    static const bool noCopies = debugEnv("PETARI_NOCOPY") != nullptr;
    if (noCopies && !((it.copy[9] >> 14) & 1)) return;

    const uint32_t* cp = it.copy;
    uint32_t tl = cp[1], wh = cp[2], ctrl = cp[9];
    int x = (int)(tl & 0x3FF), y = (int)((tl >> 10) & 0x3FF);
    int w = (int)(wh & 0x3FF) + 1, h = (int)((wh >> 10) & 0x3FF) + 1;
    bool toXfb = (ctrl >> 14) & 1;
    bool clear = (ctrl >> 11) & 1;

    int sx0 = (int)lroundf(x * fx), sx1 = (int)lroundf((x + w) * fx);
    int sy0 = (int)lroundf(y * fy), sy1 = (int)lroundf((y + h) * fy);
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx1 > target.width) sx1 = target.width;
    if (sy1 > target.height) sy1 = target.height;
    int dw = sx1 - sx0, dh = sy1 - sy0;

    if (toXfb) {
        if (dw > 0 && dh > 0) {
            VkCopyTex& xfb = getCopyTex(kXfbCopyKey, dw, dh);
            if (xfb.image != VK_NULL_HANDLE) {
                if (inRenderPass) {
                    vkCmdEndRenderPass(cmd);
                    inRenderPass = false;
                }
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = target.colorImage;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.layerCount = 1;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

                VkImageMemoryBarrier dstBarrier = barrier;
                dstBarrier.srcAccessMask = 0;
                dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                dstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                dstBarrier.image = xfb.image;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

                VkImageBlit blit{};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.layerCount = 1;
                blit.srcOffsets[0] = {sx0, sy0, 0};
                blit.srcOffsets[1] = {sx1, sy1, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.layerCount = 1;
                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {dw, dh, 1};
                vkCmdBlitImage(cmd, target.colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, xfb.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

                dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                dstBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                dstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                dstBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

                xfb.frame = p.number;
            }
        }
    } else if (sx1 > sx0 && sy1 > sy0) {
        bool half = (ctrl >> 9) & 1;
        if (half) {
            dw = (dw + 1) / 2;
            dh = (dh + 1) / 2;
        }
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        uint64_t key = copyKey(0, (cp[3] & 0xFFFFFF) << 5);
        VkCopyTex& dst = getCopyTex(key, dw, dh);
        if (dst.image != VK_NULL_HANDLE && dw > 0 && dh > 0) {
            if (inRenderPass) {
                vkCmdEndRenderPass(cmd);
                inRenderPass = false;
            }
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = target.colorImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkImageMemoryBarrier dstBarrier = barrier;
            dstBarrier.srcAccessMask = 0;
            dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dstBarrier.image = dst.image;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

            VkImageBlit blit{};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.layerCount = 1;
            blit.srcOffsets[0] = {sx0, sy0, 0};
            blit.srcOffsets[1] = {sx1, sy1, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.layerCount = 1;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {dw, dh, 1};
            vkCmdBlitImage(cmd, target.colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            dstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dstBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            dst.frame = p.number;
        }
    }

    if (!inRenderPass) {
        VkRenderPassBeginInfo resumeRpInfo = rpInfo;
        resumeRpInfo.renderPass = mEfbLoadRenderPass;
        resumeRpInfo.clearValueCount = 0;
        resumeRpInfo.pClearValues = nullptr;
        vkCmdBeginRenderPass(cmd, &resumeRpInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0, 1, &fr.set0, 0, nullptr);
        vkCmdBindIndexBuffer(cmd, fr.ibo, 0, VK_INDEX_TYPE_UINT32);
        inRenderPass = true;
    }

    if (clear && !toXfb) {
        u32 cm = it.copyCmode;
        bool colorUpd = (cm >> 3) & 1, alphaUpd = (cm >> 4) & 1, zUpd = (it.copyZmode >> 4) & 1;
        VkClearAttachment clearAtts[2]{};
        uint32_t clearAttCount = 0;
        if (colorUpd || alphaUpd) {
            clearAtts[clearAttCount].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clearAtts[clearAttCount].colorAttachment = 0;
            u32 ar = cp[6], gb = cp[7];
            clearAtts[clearAttCount].clearValue.color = {{
                (ar & 0xFF) / 255.0f,
                ((gb >> 8) & 0xFF) / 255.0f,
                (gb & 0xFF) / 255.0f,
                ((ar >> 8) & 0xFF) / 255.0f
            }};
            clearAttCount++;
        }
        if (zUpd) {
            clearAtts[clearAttCount].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            clearAtts[clearAttCount].clearValue.depthStencil = {(cp[8] & 0xFFFFFF) / 16777215.0f, 0};
            clearAttCount++;
        }
        if (clearAttCount > 0 && dw > 0 && dh > 0) {
            VkClearRect clearRect{};
            clearRect.rect.offset = {sx0, sy0};
            clearRect.rect.extent = {(uint32_t)dw, (uint32_t)dh};
            clearRect.baseArrayLayer = 0;
            clearRect.layerCount = 1;
            vkCmdClearAttachments(cmd, clearAttCount, clearAtts, 1, &clearRect);
        }
    }
}

}  // namespace gpu
