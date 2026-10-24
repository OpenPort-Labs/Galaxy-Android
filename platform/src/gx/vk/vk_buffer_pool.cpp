#include "gx/vk/vk_common.h"

namespace gpu {

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
    buffer = VK_NULL_HANDLE;
    bufferMemory = VK_NULL_HANDLE;
    if (size == 0) return false;

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
        LOGE("createBuffer: failed to find suitable memory type for size %llu", (unsigned long long)size);
        vkDestroyBuffer(mDevice, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }

    if (vkAllocateMemory(mDevice, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        LOGE("createBuffer: vkAllocateMemory failed for size %llu", (unsigned long long)memReqs.size);
        vkDestroyBuffer(mDevice, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }

    VkResult bindRes = vkBindBufferMemory(mDevice, buffer, bufferMemory, 0);
    if (bindRes != VK_SUCCESS) {
        LOGE("createBuffer: vkBindBufferMemory failed: %d", bindRes);
        vkDestroyBuffer(mDevice, buffer, nullptr);
        vkFreeMemory(mDevice, bufferMemory, nullptr);
        buffer = VK_NULL_HANDLE;
        bufferMemory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VkRenderer::uploadDynamicBuffer(VkBuffer& buf, VkDeviceMemory& mem, void*& mappedPtr, size_t& currentCap,
                                     VkBufferUsageFlags usage, const void* data, size_t bytes) {
    size_t reqCap = (bytes > 0) ? bytes : 256;
    if (reqCap > currentCap || buf == VK_NULL_HANDLE || mem == VK_NULL_HANDLE || mappedPtr == nullptr) {
        size_t newCap = reqCap + reqCap / 4 + 4096;
        VkBuffer newBuf = VK_NULL_HANDLE;
        VkDeviceMemory newMem = VK_NULL_HANDLE;

        if (!createBuffer((VkDeviceSize)newCap, usage,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          newBuf, newMem)) {
            LOGE("uploadDynamicBuffer: failed to allocate new buffer (requested %zu bytes)", newCap);
            return false;
        }

        void* newMapped = nullptr;
        VkResult mapRes = vkMapMemory(mDevice, newMem, 0, (VkDeviceSize)newCap, 0, &newMapped);
        if (mapRes != VK_SUCCESS || !newMapped) {
            LOGE("uploadDynamicBuffer: persistent vkMapMemory failed: %d", mapRes);
            vkDestroyBuffer(mDevice, newBuf, nullptr);
            vkFreeMemory(mDevice, newMem, nullptr);
            return false;
        }

        if (mem != VK_NULL_HANDLE) {
            if (mappedPtr != nullptr) vkUnmapMemory(mDevice, mem);
        }
        if (buf != VK_NULL_HANDLE) vkDestroyBuffer(mDevice, buf, nullptr);
        if (mem != VK_NULL_HANDLE) vkFreeMemory(mDevice, mem, nullptr);

        buf = newBuf;
        mem = newMem;
        mappedPtr = newMapped;
        currentCap = newCap;
    }

    if (bytes > 0 && data && mappedPtr) {
        memcpy(mappedPtr, data, bytes);
    }
    return true;
}

}  // namespace gpu
