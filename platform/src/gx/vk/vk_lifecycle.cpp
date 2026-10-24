#include "gx/vk/vk_common.h"

namespace gpu {

#if defined(VK_EXT_device_fault) && defined(VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT)
void VkRenderer::logDeviceFault(const char* where) {
    static PFN_vkGetDeviceFaultInfoEXT getFaultInfo = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        if (mDevice != VK_NULL_HANDLE) {
            getFaultInfo = (PFN_vkGetDeviceFaultInfoEXT)vkGetDeviceProcAddr(mDevice, "vkGetDeviceFaultInfoEXT");
        }
    }
    if (!getFaultInfo || mDevice == VK_NULL_HANDLE) return;

    VkDeviceFaultCountsEXT counts{};
    counts.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
    if (getFaultInfo(mDevice, &counts, nullptr) != VK_SUCCESS) return;

    VkDeviceFaultInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT;
    std::vector<VkDeviceFaultAddressInfoEXT> addrInfo(counts.addressInfoCount);
    std::vector<VkDeviceFaultVendorInfoEXT> vendorInfo(counts.vendorInfoCount);
    info.pAddressInfos = addrInfo.empty() ? nullptr : addrInfo.data();
    info.pVendorInfos = vendorInfo.empty() ? nullptr : vendorInfo.data();
    if (getFaultInfo(mDevice, &counts, &info) != VK_SUCCESS) return;

    port_log("vk: device fault during %s: %s", where, info.description);
    for (uint32_t i = 0; i < counts.addressInfoCount; i++) {
        port_log("vk:   fault address info %u: type %d, address 0x%llx, precision 0x%llx", i, (int)addrInfo[i].addressType,
                 (unsigned long long)addrInfo[i].reportedAddress, (unsigned long long)addrInfo[i].addressPrecision);
    }
    for (uint32_t i = 0; i < counts.vendorInfoCount; i++) {
        port_log("vk:   vendor fault %u: %s (code %llu, data 0x%llx)", i, vendorInfo[i].description,
                 (unsigned long long)vendorInfo[i].vendorFaultCode, (unsigned long long)vendorInfo[i].vendorFaultData);
    }
}
#else
void VkRenderer::logDeviceFault(const char* /*where*/) {
}
#endif

void VkRenderer::markFatal(const char* where, const char* what, int result) {
    if (mFatal) return;
    mFatal = true;
    if (result == VK_ERROR_DEVICE_LOST) {
        logDeviceFault(where);
        discardPipelineCacheAfterFault();
    }
    mRenderFenceInFlight = false;
    mPresentFenceInFlight = false;
    LOGE("Vulkan fatal error at frame %llu - %s: %s failed (VkResult %d); renderer disabled",
         (unsigned long long)mFrameNumber, where, what, result);
    port_log("vk: FATAL at frame %llu - %s: %s failed (VkResult %d); renderer disabled",
             (unsigned long long)mFrameNumber, where, what, result);
}

bool VkRenderer::waitInFlightSubmissions(const char* where) {
    if (mDevice == VK_NULL_HANDLE) return false;
    if (mFatal) {
        mRenderFenceInFlight = false;
        mPresentFenceInFlight = false;
        return false;
    }

    constexpr uint64_t kFenceWaitNs = 2000000000ull;
    bool ok = true;
    if (mRenderFenceInFlight) {
        int64_t t0 = port_host_time_ns();
        VkResult res = vkWaitForFences(mDevice, 1, &mRenderFence, VK_TRUE, kFenceWaitNs);
        port_perf_queue_wait(port_host_time_ns() - t0);
        mRenderFenceInFlight = false;
        if (res != VK_SUCCESS) {
            markFatal(where, "vkWaitForFences on mRenderFence", (int)res);
            ok = false;
        }
    }
    if (mPresentFenceInFlight) {
        int64_t t0 = port_host_time_ns();
        VkResult res = vkWaitForFences(mDevice, 1, &mPresentFence, VK_TRUE, kFenceWaitNs);
        port_perf_queue_wait(port_host_time_ns() - t0);
        mPresentFenceInFlight = false;
        if (res != VK_SUCCESS) {
            markFatal(where, "vkWaitForFences on mPresentFence", (int)res);
            ok = false;
        }
    }
    return ok;
}

static VkRenderer sVkRenderer;

VkRenderer& VkRenderer::get() {
    return sVkRenderer;
}

VkRenderer& vkRenderer() {
    return sVkRenderer;
}

void VkRenderer::shutdown() {
    std::lock_guard<std::recursive_mutex> lock(mRenderMutex);
    mInitialized = false;

    stopShaderWorker();

    if (mDevice != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(mDevice);

        if (mRenderFence != VK_NULL_HANDLE) {
            vkDestroyFence(mDevice, mRenderFence, nullptr);
            mRenderFence = VK_NULL_HANDLE;
        }
        if (mPresentFence != VK_NULL_HANDLE) {
            vkDestroyFence(mDevice, mPresentFence, nullptr);
            mPresentFence = VK_NULL_HANDLE;
        }
        mRenderFenceInFlight = false;
        mPresentFenceInFlight = false;

        if (mImageAvailableSem != VK_NULL_HANDLE) {
            vkDestroySemaphore(mDevice, mImageAvailableSem, nullptr);
            mImageAvailableSem = VK_NULL_HANDLE;
        }
        if (mRenderToPresentSem != VK_NULL_HANDLE) {
            vkDestroySemaphore(mDevice, mRenderToPresentSem, nullptr);
            mRenderToPresentSem = VK_NULL_HANDLE;
        }
        mRenderSignaledSem = false;
        for (VkSemaphore s : mRenderFinishedSems) {
            if (s != VK_NULL_HANDLE) vkDestroySemaphore(mDevice, s, nullptr);
        }
        mRenderFinishedSems.clear();

        for (const auto& sb : mStagingBuffersToFree) {
            if (sb.buffer != VK_NULL_HANDLE) vkDestroyBuffer(mDevice, sb.buffer, nullptr);
            if (sb.memory != VK_NULL_HANDLE) vkFreeMemory(mDevice, sb.memory, nullptr);
        }
        mStagingBuffersToFree.clear();

        if (mDummySampler != VK_NULL_HANDLE) {
            vkDestroySampler(mDevice, mDummySampler, nullptr);
            mDummySampler = VK_NULL_HANDLE;
        }
        if (mDummyTexture.view != VK_NULL_HANDLE) {
            vkDestroyImageView(mDevice, mDummyTexture.view, nullptr);
            mDummyTexture.view = VK_NULL_HANDLE;
        }
        if (mDummyTexture.image != VK_NULL_HANDLE) {
            vkDestroyImage(mDevice, mDummyTexture.image, nullptr);
            mDummyTexture.image = VK_NULL_HANDLE;
        }
        if (mDummyTexture.memory != VK_NULL_HANDLE) {
            vkFreeMemory(mDevice, mDummyTexture.memory, nullptr);
            mDummyTexture.memory = VK_NULL_HANDLE;
        }
        mDummyTexture = {};

        flushDeferredDeletes();

        for (auto& pair : mCopies) {
            if (pair.second.view != VK_NULL_HANDLE) vkDestroyImageView(mDevice, pair.second.view, nullptr);
            if (pair.second.image != VK_NULL_HANDLE) vkDestroyImage(mDevice, pair.second.image, nullptr);
            if (pair.second.memory != VK_NULL_HANDLE) vkFreeMemory(mDevice, pair.second.memory, nullptr);
        }
        mCopies.clear();

        for (auto& pair : mSamplers) {
            if (pair.second) vkDestroySampler(mDevice, pair.second, nullptr);
        }
        mSamplers.clear();

        for (auto& pair : mPipelines) {
            if (pair.second) vkDestroyPipeline(mDevice, pair.second, nullptr);
        }
        mPipelines.clear();
        mPipelineMru.reset();

        mSamplerDescSets.clear();
        mPendingDescSetFrees.clear();
        mSamplerPoolResetPending = false;

        savePipelineCache();
        if (mPipelineCache != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(mDevice, mPipelineCache, nullptr);
            mPipelineCache = VK_NULL_HANDLE;
        }

        destroyPresentationPipeline();
        destroyUiPipeline();

        auto destroyFrameBuffer = [this](VkBuffer& buf, VkDeviceMemory& mem, void*& mapped) {
            if (mapped && mem != VK_NULL_HANDLE) vkUnmapMemory(mDevice, mem);
            if (buf != VK_NULL_HANDLE) vkDestroyBuffer(mDevice, buf, nullptr);
            if (mem != VK_NULL_HANDLE) vkFreeMemory(mDevice, mem, nullptr);
            buf = VK_NULL_HANDLE;
            mem = VK_NULL_HANDLE;
            mapped = nullptr;
        };
        for (auto& fr : mFrameRes) {
            destroyFrameBuffer(fr.vbo, fr.vboMem, fr.vboMapped);
            destroyFrameBuffer(fr.ibo, fr.iboMem, fr.iboMapped);
            destroyFrameBuffer(fr.rowBuf, fr.rowMem, fr.rowMapped);
            destroyFrameBuffer(fr.recBuf, fr.recMem, fr.recMapped);
            destroyFrameBuffer(fr.stBuf, fr.stMem, fr.stMapped);
            destroyFrameBuffer(fr.vtxRecBuf, fr.vtxRecMem, fr.vtxRecMapped);
            destroyFrameBuffer(fr.eyeUbo, fr.eyeMem, fr.eyeMapped);
            destroyFrameBuffer(fr.uiVbo, fr.uiVboMem, fr.uiVboMapped);
            fr = {};
        }

        if (mPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
            mPipelineLayout = VK_NULL_HANDLE;
        }
        if (mDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(mDevice, mDescriptorPool, nullptr);
            mDescriptorPool = VK_NULL_HANDLE;
        }
        if (mStaticDescPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(mDevice, mStaticDescPool, nullptr);
            mStaticDescPool = VK_NULL_HANDLE;
        }
        if (mStorageDescLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(mDevice, mStorageDescLayout, nullptr);
            mStorageDescLayout = VK_NULL_HANDLE;
        }
        if (mTextureDescLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(mDevice, mTextureDescLayout, nullptr);
            mTextureDescLayout = VK_NULL_HANDLE;
        }

        if (mCommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(mDevice, mCommandPool, nullptr);
            mCommandPool = VK_NULL_HANDLE;
        }

        for (auto fb : mSwapchainFramebuffers) vkDestroyFramebuffer(mDevice, fb, nullptr);
        mSwapchainFramebuffers.clear();

        for (auto iv : mSwapchainImageViews) vkDestroyImageView(mDevice, iv, nullptr);
        mSwapchainImageViews.clear();

        if (mEfbRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(mDevice, mEfbRenderPass, nullptr);
            mEfbRenderPass = VK_NULL_HANDLE;
        }
        if (mEfbLoadRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(mDevice, mEfbLoadRenderPass, nullptr);
            mEfbLoadRenderPass = VK_NULL_HANDLE;
        }
        if (mPresentRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(mDevice, mPresentRenderPass, nullptr);
            mPresentRenderPass = VK_NULL_HANDLE;
        }

        if (mSwapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
            mSwapchain = VK_NULL_HANDLE;
        }
        vkDestroyDevice(mDevice, nullptr);
        mDevice = VK_NULL_HANDLE;
        ++sVkDeviceGeneration;
    }

    if (mInstance != VK_NULL_HANDLE) {
        if (mSurface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
            mSurface = VK_NULL_HANDLE;
        }
        vkDestroyInstance(mInstance, nullptr);
        mInstance = VK_NULL_HANDLE;
    }

    mInstance = VK_NULL_HANDLE;
    mDevice = VK_NULL_HANDLE;
    mSurface = VK_NULL_HANDLE;
    mSwapchain = VK_NULL_HANDLE;
    mInitialized = false;
    mFrameNumber = 0;
    mFrameIndex = 0;
    mPipelineMruHits = 0;
    mPipelineMruMisses = 0;
    mRuntimePipelineCreates = 0;
    mRuntimeCreatesKnownUid = 0;
    mCurrentFrame.reset();
    mPrepared.reset();
    LOGI("Vulkan 1.0 renderer shut down cleanly.");
}

}  // namespace gpu
