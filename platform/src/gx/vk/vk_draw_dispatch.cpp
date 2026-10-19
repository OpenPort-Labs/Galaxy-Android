#include "gx/vk/vk_common.h"

namespace gpu {

void VkRenderer::executeDrawPass(VkCommandBuffer cmd, const Frame& f, const Prepared& p,
                                 VkTarget& target, HudMode hudMode, FrameResource& fr) {
    const std::vector<Item>& items = p.items;
    float fx = (float)target.width / (float)p.nativeW;
    float fy = (float)target.height / (float)p.nativeH;

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

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0, 1, &fr.set0, 0, nullptr);
    vkCmdBindIndexBuffer(cmd, fr.ibo, 0, VK_INDEX_TYPE_UINT32);

    bool inRenderPass = true;
    bool inHudSection = false;

    VkPipeline lastPipeline = VK_NULL_HANDLE;
    VkDescriptorSet lastSet1 = VK_NULL_HANDLE;
    VkDeviceSize lastVOffset = ~(VkDeviceSize)0;
    int lastVx = -1, lastVy = -1, lastVw = -1, lastVh = -1;
    int lastSx = -1, lastSy = -1, lastSw = -1, lastSh = -1;
    uint32_t lastTexMask = ~0u;
    uint32_t lastTex[8]{};
    uint32_t lastTexMode0[8]{};
    uint32_t lastTexMode1[8]{};
    bool hasLastTex = false;

    for (const Item& it : items) {
        if (it.type == IT_MARKER) {
            inHudSection = (it.marker == MARK_HUD_BEGIN);
            continue;
        }
        if (inHudSection && hudMode == HudMode::Skip) {
            continue;
        }

        if (it.type == IT_COPY) {
            executeCopyItem(cmd, it, p, target, fx, fy, inRenderPass, rpInfo, fr);
            lastPipeline = VK_NULL_HANDLE;
            lastSet1 = VK_NULL_HANDLE;
            lastVOffset = ~(VkDeviceSize)0;
            lastVx = lastVy = lastVw = lastVh = -1;
            lastSx = lastSy = lastSw = lastSh = -1;
            hasLastTex = false;
            continue;
        }

        if (it.type != IT_DRAW || !it.idxCount) {
            continue;
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
            lastPipeline = VK_NULL_HANDLE;
            lastSet1 = VK_NULL_HANDLE;
            lastVOffset = ~(VkDeviceSize)0;
            lastVx = lastVy = lastVw = lastVh = -1;
            lastSx = lastSy = lastSw = lastSh = -1;
            hasLastTex = false;
        }

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
        vy = (int)lroundf(it.vp[1] * fy);

        int st = (int)lroundf(it.sc[1] * fy);
        int sb = (int)lroundf(it.sc[3] * fy);
        sy = st;
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

        if (vx != lastVx || vy != lastVy || vw != lastVw || vh != lastVh) {
            VkViewport viewport{};
            viewport.x = (float)vx;
            viewport.y = (float)vy;
            viewport.width = (float)vw;
            viewport.height = (float)vh;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            lastVx = vx; lastVy = vy; lastVw = vw; lastVh = vh;
        }

        if (sx != lastSx || sy != lastSy || sw != lastSw || sh != lastSh) {
            VkRect2D scissor{};
            scissor.offset = {sx, sy};
            scissor.extent = {(uint32_t)sw, (uint32_t)sh};
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            lastSx = sx; lastSy = sy; lastSw = sw; lastSh = sh;
        }

        if (it.uid >= p.uids.size()) {
            LOGW("VkRenderer: draw references shader uid %u out of range (%zu)", it.uid, p.uids.size());
            continue;
        }
        VkPipeline pipeline = getPipeline(it, p.uids[it.uid]);
        if (pipeline == VK_NULL_HANDLE) continue;
        if (pipeline != lastPipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            lastPipeline = pipeline;
        }

        vkCmdPushConstants(cmd, mPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &it.firstOrdinal);

        VkDescriptorSet set1 = VK_NULL_HANDLE;
        bool sameTex = hasLastTex && (it.texMask == lastTexMask) &&
                       (memcmp(it.tex, lastTex, sizeof(lastTex)) == 0) &&
                       (memcmp(it.texMode0, lastTexMode0, sizeof(lastTexMode0)) == 0) &&
                       (memcmp(it.texMode1, lastTexMode1, sizeof(lastTexMode1)) == 0);
        if (sameTex && lastSet1 != VK_NULL_HANDLE) {
            set1 = lastSet1;
        } else {
            std::array<VkImageView, 8> views;
            std::array<VkSampler, 8> samplers;
            for (int u = 0; u < 8; u++) {
                uint32_t tr = it.tex[u];
                if (!(it.texMask & (1u << u)) || tr == ~0u) {
                    views[u] = mDummyTexture.view;
                    samplers[u] = mDummySampler;
                    continue;
                }
                if (tr & 0x80000000u) {
                    uint32_t addr = (tr & 0x7FFFFFFF) << 5;
                    uint64_t key = copyKey(0, addr);
                    auto cit = mCopies.find(key);
                    if (cit != mCopies.end() && cit->second.view != VK_NULL_HANDLE) {
                        views[u] = cit->second.view;
                        samplers[u] = getSampler(it.texMode0[u], it.texMode1[u], 1);
                    } else {
                        views[u] = mDummyTexture.view;
                        samplers[u] = mDummySampler;
                    }
                } else if (tr < f.textures.size()) {
                    const std::shared_ptr<TexImage>& ti = f.textures[tr];
                    if (ti && ti->vkView != 0 && ti->vkVersion == ti->version && ti->vkGeneration == sVkDeviceGeneration) {
                        views[u] = (VkImageView)ti->vkView;
                        samplers[u] = getSampler(it.texMode0[u], it.texMode1[u], ti->levels);
                    } else {
                        views[u] = mDummyTexture.view;
                        samplers[u] = mDummySampler;
                    }
                } else {
                    views[u] = mDummyTexture.view;
                    samplers[u] = mDummySampler;
                }
            }
            set1 = getSamplerDescriptorSet(views, samplers);
            if (set1 == VK_NULL_HANDLE) {
                LOGE("VkRenderer: failed to allocate sampler descriptor set - skipping draw");
                continue;
            }
            lastTexMask = it.texMask;
            memcpy(lastTex, it.tex, sizeof(lastTex));
            memcpy(lastTexMode0, it.texMode0, sizeof(lastTexMode0));
            memcpy(lastTexMode1, it.texMode1, sizeof(lastTexMode1));
            hasLastTex = true;
        }

        if (set1 != lastSet1) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 1, 1, &set1, 0, nullptr);
            lastSet1 = set1;
        }

        VkDeviceSize vOffset = (VkDeviceSize)it.firstWord * 4;
        if (vOffset != lastVOffset) {
            vkCmdBindVertexBuffers(cmd, 0, 1, &fr.vbo, &vOffset);
            lastVOffset = vOffset;
        }

        vkCmdDrawIndexed(cmd, it.idxCount, 1, it.idxOffset, 0, 0);
    }

    if (inRenderPass) {
        vkCmdEndRenderPass(cmd);
        inRenderPass = false;
        target.colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else if (target.colorLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = target.colorLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = target.colorImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        target.colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
}

}  // namespace gpu
