#include "gx/vk/vk_common.h"

namespace gpu {

static uint64_t sUploadAttempted = 0;
static uint64_t sUploadFailed = 0;
static uint64_t sUploadRecorded = 0;
static uint64_t sUploadBytes = 0;

void VkRenderer::uploadTextures(VkCommandBuffer cmd, const std::vector<std::shared_ptr<TexImage>>& textures) {
    if (cmd == VK_NULL_HANDLE) return;
    struct UploadItem {
        std::shared_ptr<TexImage> img;
        VkTexture vt;
        VkBuffer stagingBuf;
        VkDeviceMemory stagingMem;
        VkDeviceSize stagingSize;
    };
    std::vector<UploadItem> pending;

    for (const auto& img : textures) {
        if (!img || img->rgba.empty() || img->width == 0 || img->height == 0) continue;
        sUploadAttempted++;

        if (img->width > 4096 || img->height > 4096 || img->levels > 14) {
            LOGE("uploadTextures: texture dimensions %ux%u (%u levels) out of hardware limits",
                 img->width, img->height, img->levels);
            sUploadFailed++;
            continue;
        }

        const uint32_t levels = img->levels ? img->levels : 1;
        VkDeviceSize requiredBytes = 0;
        uint32_t cw = img->width, ch = img->height;
        bool overflow = false;
        for (uint32_t l = 0; l < levels; l++) {
            VkDeviceSize levelBytes = (VkDeviceSize)cw * ch * 4;
            if (levelBytes == 0 || (levelBytes / (cw * 4) != ch)) {
                overflow = true;
                break;
            }
            requiredBytes += levelBytes;
            cw = cw > 1 ? cw / 2 : 1;
            ch = ch > 1 ? ch / 2 : 1;
        }

        if (overflow) {
            LOGE("uploadTextures: arithmetic overflow calculating required size for %ux%u", img->width, img->height);
            sUploadFailed++;
            continue;
        }

        const size_t totalBytes = img->rgba.size() * sizeof(uint32_t);
        if (totalBytes < (size_t)requiredBytes) {
            LOGE("uploadTextures: buffer size mismatch for %ux%u (%u levels): rgba has %zu bytes, need %llu",
                 img->width, img->height, levels, totalBytes, (unsigned long long)requiredBytes);
            sUploadFailed++;
            continue;
        }

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
        vt.levels = levels;
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
            sUploadFailed++;
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
            sUploadFailed++;
            continue;
        }
        if (vkBindImageMemory(mDevice, vt.image, vt.memory, 0) != VK_SUCCESS) {
            LOGE("uploadTextures: vkBindImageMemory failed");
            vkFreeMemory(mDevice, vt.memory, nullptr);
            vkDestroyImage(mDevice, vt.image, nullptr);
            sUploadFailed++;
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
            sUploadFailed++;
            continue;
        }

        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        const VkDeviceSize uploadBytes = requiredBytes;
        if (!createBuffer(uploadBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          stagingBuf, stagingMem)) {
            LOGE("uploadTextures: createBuffer failed for staging buffer (%llu bytes)", (unsigned long long)uploadBytes);
            vkDestroyImageView(mDevice, vt.view, nullptr);
            vkFreeMemory(mDevice, vt.memory, nullptr);
            vkDestroyImage(mDevice, vt.image, nullptr);
            sUploadFailed++;
            continue;
        }

        void* mapped = nullptr;
        VkResult mapRes = vkMapMemory(mDevice, stagingMem, 0, uploadBytes, 0, &mapped);
        if (mapRes != VK_SUCCESS || !mapped) {
            LOGE("uploadTextures: vkMapMemory failed (%d) for staging buffer (%llu bytes)", (int)mapRes, (unsigned long long)uploadBytes);
            vkDestroyBuffer(mDevice, stagingBuf, nullptr);
            vkFreeMemory(mDevice, stagingMem, nullptr);
            vkDestroyImageView(mDevice, vt.view, nullptr);
            vkFreeMemory(mDevice, vt.memory, nullptr);
            vkDestroyImage(mDevice, vt.image, nullptr);
            sUploadFailed++;
            continue;
        }

        memcpy(mapped, img->rgba.data(), uploadBytes);
        vkUnmapMemory(mDevice, stagingMem);
        pending.push_back({img, vt, stagingBuf, stagingMem, uploadBytes});
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
        bool regionSafe = true;
        for (uint32_t l = 0; l < item.vt.levels; l++) {
            VkDeviceSize levelBytes = (VkDeviceSize)w * h * 4;
            if (offset + levelBytes > item.stagingSize) {
                LOGE("uploadTextures: copy region bounds overflow level %u: offset %llu + bytes %llu > total %llu",
                     l, (unsigned long long)offset, (unsigned long long)levelBytes, (unsigned long long)item.stagingSize);
                regionSafe = false;
                break;
            }
            regions[l].bufferOffset = offset;
            regions[l].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            regions[l].imageSubresource.mipLevel = l;
            regions[l].imageSubresource.layerCount = 1;
            regions[l].imageExtent = {w, h, 1};
            offset += levelBytes;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        if (!regionSafe) {
            sUploadFailed++;
            continue;
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
        sUploadRecorded++;
        sUploadBytes += (item.img->rgba.size() * sizeof(uint32_t));
    }

    if (sUploadRecorded % 200 == 0) {
        LOGI("VkRenderer textures: %llu attempted, %llu recorded, %llu failed, %llu KB staged",
             (unsigned long long)sUploadAttempted, (unsigned long long)sUploadRecorded,
             (unsigned long long)sUploadFailed, (unsigned long long)(sUploadBytes / 1024));
    }
}

}  // namespace gpu
