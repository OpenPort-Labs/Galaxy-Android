#include "gx/vk/vk_common.h"

namespace gpu {

VkSampler VkRenderer::getSampler(uint32_t mode0, uint32_t mode1, uint32_t levels) {
    uint64_t key = ((uint64_t)(mode0 & 0xFFFFFF) << 32) | ((uint64_t)(mode1 & 0xFFFF) << 8) | (levels & 0xFF);
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

    if (!mips) {
        sci.minLod = 0.0f;
        sci.maxLod = 0.0f;
    } else {
        sci.minLod = std::min((float)(levels - 1), (mode1 & 0xFF) / 16.0f);
        sci.maxLod = std::min((float)(levels - 1), ((mode1 >> 8) & 0xFF) / 16.0f);
        if (sci.maxLod < sci.minLod) sci.maxLod = sci.minLod;
    }

    VkSampler s = VK_NULL_HANDLE;
    vkCreateSampler(mDevice, &sci, nullptr, &s);
    mSamplers[key] = s;
    return s;
}

void VkRenderer::uploadTextures(VkCommandBuffer cmd, const std::vector<std::shared_ptr<TexImage>>& textures) {
    if (cmd == VK_NULL_HANDLE) return;
    struct UploadItem {
        std::shared_ptr<TexImage> img;
        VkTexture vt;
        VkBuffer stagingBuf;
        VkDeviceMemory stagingMem;
    };
    std::vector<UploadItem> pending;

    for (const auto& img : textures) {
        if (!img || img->rgba.empty() || img->width == 0 || img->height == 0) continue;
        bool sameDevice = (img->vkGeneration == sVkDeviceGeneration);
        if (img->vkImage != 0 && img->vkVersion == img->version && sameDevice) {
            continue;
        }

        if (img->vkView != 0 || img->vkImage != 0 || img->vkMemory != 0) {
            if (sameDevice) {
                if (img->vkView) vkDestroyImageView(mDevice, (VkImageView)img->vkView, nullptr);
                if (img->vkImage) vkDestroyImage(mDevice, (VkImage)img->vkImage, nullptr);
                if (img->vkMemory) vkFreeMemory(mDevice, (VkDeviceMemory)img->vkMemory, nullptr);
            }
            img->vkView = img->vkImage = img->vkMemory = 0;
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

        if (vkCreateImage(mDevice, &ici, nullptr, &vt.image) != VK_SUCCESS) {
            LOGE("uploadTextures: vkCreateImage failed %ux%u", vt.width, vt.height);
            continue;
        }

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(mDevice, vt.image, &memReqs);

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReqs.size;
        mai.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mai.memoryTypeIndex == ~0u) {
            mai.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, 0);
        }
        if (mai.memoryTypeIndex == ~0u || vkAllocateMemory(mDevice, &mai, nullptr, &vt.memory) != VK_SUCCESS) {
            LOGE("uploadTextures: vkAllocateMemory failed (memType=%u, size=%llu)", mai.memoryTypeIndex, (unsigned long long)memReqs.size);
            vkDestroyImage(mDevice, vt.image, nullptr);
            continue;
        }
        if (vkBindImageMemory(mDevice, vt.image, vt.memory, 0) != VK_SUCCESS) {
            LOGE("uploadTextures: vkBindImageMemory failed");
            vkFreeMemory(mDevice, vt.memory, nullptr);
            vkDestroyImage(mDevice, vt.image, nullptr);
            continue;
        }

        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = vt.image;
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = vt.levels;
        ivci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(mDevice, &ivci, nullptr, &vt.view) != VK_SUCCESS) {
            LOGE("uploadTextures: vkCreateImageView failed");
            vkFreeMemory(mDevice, vt.memory, nullptr);
            vkDestroyImage(mDevice, vt.image, nullptr);
            continue;
        }

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
            pending.push_back({img, vt, stagingBuf, stagingMem});
        } else {
            vkDestroyImageView(mDevice, vt.view, nullptr);
            vkFreeMemory(mDevice, vt.memory, nullptr);
            vkDestroyImage(mDevice, vt.image, nullptr);
        }
    }

    if (pending.empty()) return;

    for (const auto& item : pending) {
        VkImageMemoryBarrier barrierToDst{};
        barrierToDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrierToDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrierToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrierToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrierToDst.image = item.vt.image;
        barrierToDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrierToDst.subresourceRange.levelCount = item.vt.levels;
        barrierToDst.subresourceRange.layerCount = 1;
        barrierToDst.srcAccessMask = 0;
        barrierToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToDst);

        std::vector<VkBufferImageCopy> regions(item.vt.levels);
        VkDeviceSize offset = 0;
        uint32_t w = item.vt.width, h = item.vt.height;
        for (uint32_t l = 0; l < item.vt.levels; l++) {
            regions[l].bufferOffset = offset;
            regions[l].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            regions[l].imageSubresource.mipLevel = l;
            regions[l].imageSubresource.layerCount = 1;
            regions[l].imageExtent = {w, h, 1};
            offset += (VkDeviceSize)w * h * 4;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }

        vkCmdCopyBufferToImage(cmd, item.stagingBuf, item.vt.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               (uint32_t)regions.size(), regions.data());

        VkImageMemoryBarrier barrierToShader = barrierToDst;
        barrierToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrierToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrierToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrierToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToShader);

        item.img->vkImage = (uint64_t)item.vt.image;
        item.img->vkMemory = (uint64_t)item.vt.memory;
        item.img->vkView = (uint64_t)item.vt.view;
        item.img->vkVersion = item.vt.version;
        item.img->vkGeneration = sVkDeviceGeneration;

        mStagingBuffersToFree.push_back({item.stagingBuf, item.stagingMem});
    }
}

}  // namespace gpu
