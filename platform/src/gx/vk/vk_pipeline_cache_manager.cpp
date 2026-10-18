#include "gx/vk/vk_common.h"

namespace gpu {

static char sPipelineCachePath[512] = "";

void setVkPipelineCachePath(const char* path) {
    if (path && path[0]) {
        snprintf(sPipelineCachePath, sizeof(sPipelineCachePath), "%s", path);
        LOGI("Vulkan pipeline cache path configured: %s", sPipelineCachePath);
    }
}

static const char* getPipelineCachePath() {
    if (!sPipelineCachePath[0]) {
        snprintf(sPipelineCachePath, sizeof(sPipelineCachePath), "/data/data/com.openport.galaxyandroid/files/vk_pipeline_cache.bin");
    }
    return sPipelineCachePath;
}

struct VkPipelineCacheHeaderVersionOne {
    uint32_t headerSize;
    uint32_t headerVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint8_t  pipelineCacheUUID[VK_UUID_SIZE];
};

void VkRenderer::initPipelineCache() {
    if (mPipelineCache != VK_NULL_HANDLE || mDevice == VK_NULL_HANDLE) return;

    std::vector<char> cacheData;
    FILE* f = fopen(getPipelineCachePath(), "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        const long kMaxPipelineCacheBytes = 32l * 1024 * 1024;
        if (sz >= (long)sizeof(VkPipelineCacheHeaderVersionOne) && sz <= kMaxPipelineCacheBytes) {
            cacheData.resize(sz);
            if (fread(cacheData.data(), 1, sz, f) != (size_t)sz) {
                cacheData.clear();
            }
        }
        fclose(f);
    }

    if (cacheData.size() >= sizeof(VkPipelineCacheHeaderVersionOne)) {
        VkPipelineCacheHeaderVersionOne hdr;
        memcpy(&hdr, cacheData.data(), sizeof(hdr));
        if (hdr.headerSize < sizeof(VkPipelineCacheHeaderVersionOne) ||
            hdr.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE ||
            hdr.vendorID != mDeviceProperties.vendorID ||
            hdr.deviceID != mDeviceProperties.deviceID ||
            memcmp(hdr.pipelineCacheUUID, mDeviceProperties.pipelineCacheUUID, VK_UUID_SIZE) != 0) {
            LOGW("Vulkan pipeline cache header mismatch (vendor: 0x%x vs 0x%x, device: 0x%x vs 0x%x); discarding stale cache",
                 hdr.vendorID, mDeviceProperties.vendorID, hdr.deviceID, mDeviceProperties.deviceID);
            cacheData.clear();
        }
    } else {
        cacheData.clear();
    }

    VkPipelineCacheCreateInfo pcci{};
    pcci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    if (!cacheData.empty()) {
        pcci.initialDataSize = cacheData.size();
        pcci.pInitialData = cacheData.data();
        LOGI("Loading validated Vulkan pipeline cache (%zu bytes)", cacheData.size());
    }

    if (vkCreatePipelineCache(mDevice, &pcci, nullptr, &mPipelineCache) != VK_SUCCESS) {
        LOGW("Failed to create Vulkan pipeline cache; attempting empty cache creation");
        pcci.initialDataSize = 0;
        pcci.pInitialData = nullptr;
        if (vkCreatePipelineCache(mDevice, &pcci, nullptr, &mPipelineCache) != VK_SUCCESS) {
            mPipelineCache = VK_NULL_HANDLE;
        }
    }
}

void VkRenderer::savePipelineCache() {
    if (mPipelineCache == VK_NULL_HANDLE || mDevice == VK_NULL_HANDLE) return;

    size_t dataSize = 0;
    if (vkGetPipelineCacheData(mDevice, mPipelineCache, &dataSize, nullptr) != VK_SUCCESS || dataSize == 0) {
        return;
    }

    std::vector<char> data(dataSize);
    if (vkGetPipelineCacheData(mDevice, mPipelineCache, &dataSize, data.data()) == VK_SUCCESS) {
        const std::string tmpPath = std::string(getPipelineCachePath()) + ".tmp";
        FILE* f = fopen(tmpPath.c_str(), "wb");
        if (f) {
            bool ok = fwrite(data.data(), 1, dataSize, f) == dataSize;
            if (fclose(f) != 0) ok = false;
            if (ok && rename(tmpPath.c_str(), getPipelineCachePath()) == 0) {
                LOGI("Saved Vulkan pipeline cache (%zu bytes)", dataSize);
            } else {
                remove(tmpPath.c_str());
                LOGW("Could not store the Vulkan pipeline cache; continuing without it");
            }
        }
    }
}

void VkRenderer::discardPipelineCacheAfterFault() {
    const char* path = getPipelineCachePath();
    if (remove(path) == 0) {
        port_log("vk: dropped the driver pipeline cache after a device loss (%s)", path);
    }
    const std::string tmpPath = std::string(path) + ".tmp";
    remove(tmpPath.c_str());
}

}  // namespace gpu
