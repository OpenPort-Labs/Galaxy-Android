#include "gx/vk/vk_common.h"

namespace gpu {

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
        colorMemType = findMemoryType(memReqs.memoryTypeBits, 0);
    }
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
        depthMemType = findMemoryType(memReqs.memoryTypeBits, 0);
    }
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

    t.colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    t.hasValidContent = false;
    return t;
}

void VkRenderer::destroyTarget(VkTarget& target) {
    std::lock_guard<std::recursive_mutex> lock(mRenderMutex);
    waitInFlightSubmissions("destroyTarget");
    if (mDevice != VK_NULL_HANDLE) {
        if (target.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(mDevice, target.framebuffer, nullptr);
            target.framebuffer = VK_NULL_HANDLE;
        }
        if (target.colorView != VK_NULL_HANDLE) {
            auto it = mPresentDescSets.find(target.colorView);
            if (it != mPresentDescSets.end()) {
                if (it->second != VK_NULL_HANDLE && mStaticDescPool != VK_NULL_HANDLE) {
                    vkFreeDescriptorSets(mDevice, mStaticDescPool, 1, &it->second);
                }
                mPresentDescSets.erase(it);
            }
            vkDestroyImageView(mDevice, target.colorView, nullptr);
            target.colorView = VK_NULL_HANDLE;
        }
        if (target.colorImage != VK_NULL_HANDLE) {
            vkDestroyImage(mDevice, target.colorImage, nullptr);
            target.colorImage = VK_NULL_HANDLE;
        }
        if (target.colorMemory != VK_NULL_HANDLE) {
            vkFreeMemory(mDevice, target.colorMemory, nullptr);
            target.colorMemory = VK_NULL_HANDLE;
        }
        if (target.depthView != VK_NULL_HANDLE) {
            vkDestroyImageView(mDevice, target.depthView, nullptr);
            target.depthView = VK_NULL_HANDLE;
        }
        if (target.depthImage != VK_NULL_HANDLE) {
            vkDestroyImage(mDevice, target.depthImage, nullptr);
            target.depthImage = VK_NULL_HANDLE;
        }
        if (target.depthMemory != VK_NULL_HANDLE) {
            vkFreeMemory(mDevice, target.depthMemory, nullptr);
            target.depthMemory = VK_NULL_HANDLE;
        }
    }
    target = {};
}

void VkRenderer::invalidateSamplerDescSetsForView(VkImageView view) {
    if (view == VK_NULL_HANDLE) return;
    bool erased = false;
    for (auto it = mSamplerDescSets.begin(); it != mSamplerDescSets.end();) {
        bool hasView = false;
        for (int u = 0; u < 8; u++) {
            if (it->first.views[u] == view) { hasView = true; break; }
        }
        if (hasView) {
            if (it->second.set != VK_NULL_HANDLE && mDescriptorPool != VK_NULL_HANDLE) {
                vkFreeDescriptorSets(mDevice, mDescriptorPool, 1, &it->second.set);
            }
            it = mSamplerDescSets.erase(it);
            erased = true;
        } else {
            ++it;
        }
    }
    // Any draw loop holding one of the freed sets must resolve it again.
    if (erased) mSamplerDescEpoch++;
}

VkCopyTex& VkRenderer::getCopyTex(uint64_t key, int w, int h) {
    VkCopyTex& c = mCopies[key];
    if (c.image != VK_NULL_HANDLE && (c.w != w || c.h != h)) {
        invalidateSamplerDescSetsForView(c.view);
        queueVkTextureDelete((uint64_t)c.image, (uint64_t)c.memory, (uint64_t)c.view);
        c.view = VK_NULL_HANDLE;
        c.image = VK_NULL_HANDLE;
        c.memory = VK_NULL_HANDLE;
    }
    if (c.image == VK_NULL_HANDLE) {
        c.w = w;
        c.h = h;
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
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(mDevice, &ici, nullptr, &c.image) == VK_SUCCESS) {
            VkMemoryRequirements mr;
            vkGetImageMemoryRequirements(mDevice, c.image, &mr);
            uint32_t memType = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (memType == ~0u) {
                memType = findMemoryType(mr.memoryTypeBits, 0);
            }
            if (memType != ~0u) {
                VkMemoryAllocateInfo ai{};
                ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                ai.allocationSize = mr.size;
                ai.memoryTypeIndex = memType;
                if (vkAllocateMemory(mDevice, &ai, nullptr, &c.memory) == VK_SUCCESS &&
                    vkBindImageMemory(mDevice, c.image, c.memory, 0) == VK_SUCCESS) {
                    VkImageViewCreateInfo ivci{};
                    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                    ivci.image = c.image;
                    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
                    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    ivci.subresourceRange.levelCount = 1;
                    ivci.subresourceRange.layerCount = 1;
                    vkCreateImageView(mDevice, &ivci, nullptr, &c.view);
                }
            }
        }
    }
    return c;
}

void VkRenderer::evictCopies(uint64_t currentFrame) {
    const uint64_t kKeepFrames = 16;
    for (auto it = mCopies.begin(); it != mCopies.end();) {
        if (it->first != kXfbCopyKey && it->second.frame + kKeepFrames < currentFrame) {
            invalidateSamplerDescSetsForView(it->second.view);
            queueVkTextureDelete((uint64_t)it->second.image, (uint64_t)it->second.memory, (uint64_t)it->second.view);
            it = mCopies.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace gpu
