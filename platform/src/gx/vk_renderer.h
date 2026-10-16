#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/native_window.h>
#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <unordered_map>
#include <array>
#include <cstring>

#include "gx_record.h"
#include "gpu.h"
#include "gl_renderer.h"
#include "gx_builder.h"
#include "vk_shadergen.h"
#include "mobile/mobile_ui.h"

namespace gpu {

struct VkTarget {
    int width = 0;
    int height = 0;
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkImageLayout colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool hasValidContent = false;
};

struct VkCopyTex {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    int w = 0;
    int h = 0;
    uint64_t frame = 0;
};

struct PipelineKey {
    ShaderUid uid;
    uint32_t flags;
    uint32_t depthState;   // test(1) | (write << 1) | (depthFunc << 4)
    // Blend factors are stored as the mapped VkBlendFactor (0..14), not as the
    // GL enums: GL_FUNC_* and the 0x03xx/0x80xx factors overlap in their low
    // bits, which used to make distinct blend states share one pipeline.
    uint32_t blendState;   // blendOn(1) | (srcRGB << 1) | (dstRGB << 6) | (srcAlpha << 11) | (dstAlpha << 16)
    uint32_t blendState2;  // colorOp(2) | (alphaOp << 2) | (colorMask << 4) | (constantAlpha << 8)
    uint32_t rasterState;  // cull(1) | (cullFace << 1) | (prim << 4)

    bool operator==(const PipelineKey& o) const {
        return uid == o.uid && flags == o.flags && depthState == o.depthState &&
               blendState == o.blendState && blendState2 == o.blendState2 && rasterState == o.rasterState;
    }
};

struct PipelineKeyHash {
    size_t operator()(const PipelineKey& k) const {
        size_t h = ShaderUidHash()(k.uid);
        h = (h ^ k.flags) * 1099511628211ull;
        h = (h ^ k.depthState) * 1099511628211ull;
        h = (h ^ k.blendState) * 1099511628211ull;
        h = (h ^ k.blendState2) * 1099511628211ull;
        h = (h ^ k.rasterState) * 1099511628211ull;
        return h;
    }
};

struct SamplerDescKey {
    std::array<VkImageView, 8> views;
    std::array<VkSampler, 8> samplers;

    bool operator==(const SamplerDescKey& o) const {
        return views == o.views && samplers == o.samplers;
    }
};

struct SamplerDescKeyHash {
    size_t operator()(const SamplerDescKey& k) const {
        size_t h = 1469598103934665603ull;
        for (int i = 0; i < 8; i++) {
            h = (h ^ (uintptr_t)k.views[i]) * 1099511628211ull;
            h = (h ^ (uintptr_t)k.samplers[i]) * 1099511628211ull;
        }
        return h;
    }
};

// One cached descriptor set per distinct (views, samplers) combination, with the
// frame it was last used in so unused sets can be freed again instead of
// draining the descriptor pool.
struct SamplerSetEntry {
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint64_t lastUse = 0;
};

struct FrameResource {
    VkBuffer vbo = VK_NULL_HANDLE;
    VkDeviceMemory vboMem = VK_NULL_HANDLE;
    size_t vboCap = 0;

    VkBuffer ibo = VK_NULL_HANDLE;
    VkDeviceMemory iboMem = VK_NULL_HANDLE;
    size_t iboCap = 0;

    VkBuffer rowBuf = VK_NULL_HANDLE;
    VkDeviceMemory rowMem = VK_NULL_HANDLE;
    size_t rowCap = 0;

    VkBuffer recBuf = VK_NULL_HANDLE;
    VkDeviceMemory recMem = VK_NULL_HANDLE;
    size_t recCap = 0;

    VkBuffer stBuf = VK_NULL_HANDLE;
    VkDeviceMemory stMem = VK_NULL_HANDLE;
    size_t stCap = 0;

    VkBuffer vtxRecBuf = VK_NULL_HANDLE;
    VkDeviceMemory vtxRecMem = VK_NULL_HANDLE;
    size_t vtxRecCap = 0;

    VkBuffer eyeUbo = VK_NULL_HANDLE;
    VkDeviceMemory eyeMem = VK_NULL_HANDLE;
    size_t eyeCap = 0;

    VkDescriptorSet set0 = VK_NULL_HANDLE;

    // UI dynamic vertex buffer
    VkBuffer uiVbo = VK_NULL_HANDLE;
    VkDeviceMemory uiVboMem = VK_NULL_HANDLE;
    size_t uiVboCap = 0;
};

struct VkTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t levels = 1;
    uint32_t version = 0;
};

class VkRenderer {
public:
    static VkRenderer& get();

    VkRenderer() = default;
    ~VkRenderer() { shutdown(); }

    bool init(ANativeWindow* window);
    void shutdown();

    bool isReady() const { return mInitialized; }
    // True once a fatal Vulkan error (lost device, out of memory, failed
    // submission) has been seen.  Rendering then stops instead of retrying every
    // frame forever, which is the caller's cue to tear the backend down and
    // continue on OpenGL ES.
    bool hasFailed() const { return mFatal; }
    int width() const { return mWidth; }
    int height() const { return mHeight; }
    const char* deviceName() const { return mDeviceProperties.deviceName; }

    VkTarget createTarget(int w, int h);
    void destroyTarget(VkTarget& target);

    bool update();
    bool render(VkTarget& target, const EyeView* eye, VkTarget* hud, HudMode hudMode = HudMode::Inline);
    uint64_t frameNumber() const { return mFrameNumber; }
    bool hasFrame() const { return mCurrentFrame != nullptr; }
    void present(const VkTarget& target, int vpX, int vpY, int vpW, int vpH);

    void onWindowResized(ANativeWindow* window);

private:
    bool createInstance();
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createSurface(ANativeWindow* window);
    bool createSwapchain();
    bool createRenderPasses();
    bool createSwapchainFramebuffers();
    bool createCommandPool();
    bool createSyncObjects();
    bool createDescriptorLayouts();
    bool createPresentationPipeline();
    void destroyPresentationPipeline();
    bool createUiPipeline();
    void destroyUiPipeline();
    void renderUiOverlay(int width, int height);
    void initPipelineCache();
    void savePipelineCache();

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkFormat findSupportedDepthFormat();
    // Destroys texture images whose TexImage died on another thread, and frees
    // descriptor sets (and other cached resources) the recent frames did not use.
    void flushDeferredDeletes();
    void trimFrameCaches(uint64_t frame);
    // Waits for every submission still in flight.  This is the only point where
    // cached GPU resources, the render target and the frame resources may be
    // touched or destroyed safely.
    bool waitInFlightSubmissions(const char* where);
    // Records a fatal Vulkan error (once) and stops rendering.
    void markFatal(const char* where, const char* what, int result);

    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    bool uploadDynamicBuffer(VkBuffer& buf, VkDeviceMemory& mem, size_t& currentCap,
                             VkBufferUsageFlags usage, const void* data, size_t bytes);

    VkSampler getSampler(uint32_t mode0, uint32_t mode1, uint32_t levels);
    void uploadTextures(const std::vector<std::shared_ptr<TexImage>>& textures);
    VkPipeline getPipeline(const Item& it, const ShaderUid& uid);
    VkDescriptorSet getSamplerDescriptorSet(const std::array<VkImageView, 8>& views,
                                            const std::array<VkSampler, 8>& samplers);

    VkShaderModule createShaderModule(const uint32_t* code, size_t sizeBytes);

    bool mInitialized = false;
    ANativeWindow* mWindow = nullptr;
    int mWidth = 0;
    int mHeight = 0;

    VkInstance mInstance = VK_NULL_HANDLE;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties mDeviceProperties{};
    VkPhysicalDeviceFeatures mDeviceFeatures{};
    VkDevice mDevice = VK_NULL_HANDLE;
    VkQueue mQueue = VK_NULL_HANDLE;
    uint32_t mQueueFamily = 0;

    VkSurfaceKHR mSurface = VK_NULL_HANDLE;
    VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;
    VkFormat mSwapchainFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageUsageFlags mSwapchainUsage = 0;
    VkSurfaceTransformFlagBitsKHR mSwapchainPreTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    // The extent the swapchain was actually created with.  The framebuffers and
    // the presentation render area have to follow it, which is not necessarily
    // the window size on every device.
    VkExtent2D mSwapchainExtent{0, 0};
    VkFormat mDepthFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> mSwapchainImages;
    std::vector<VkImageView> mSwapchainImageViews;
    std::vector<VkFramebuffer> mSwapchainFramebuffers;

    VkRenderPass mEfbRenderPass = VK_NULL_HANDLE;
    VkRenderPass mEfbLoadRenderPass = VK_NULL_HANDLE;
    VkRenderPass mPresentRenderPass = VK_NULL_HANDLE;
    VkCommandPool mCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer mCommandBuffer = VK_NULL_HANDLE;

    VkSemaphore mImageAvailableSem = VK_NULL_HANDLE;
    VkSemaphore mRenderFinishedSem = VK_NULL_HANDLE;
    VkFence mRenderFence = VK_NULL_HANDLE;
    VkFence mPresentFence = VK_NULL_HANDLE;
    bool mRenderFenceInFlight = false;
    bool mPresentFenceInFlight = false;
    bool mFatal = false;

    // Descriptors & Pipelines
    VkDescriptorSetLayout mStorageDescLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout mTextureDescLayout = VK_NULL_HANDLE;
    VkDescriptorPool mDescriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache mPipelineCache = VK_NULL_HANDLE;

    // Presentation pipeline
    VkDescriptorSetLayout mPresentDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout mPresentPipelineLayout = VK_NULL_HANDLE;
    VkPipeline mPresentPipeline = VK_NULL_HANDLE;
    VkSampler mPresentSampler = VK_NULL_HANDLE;
    std::unordered_map<VkImageView, VkDescriptorSet> mPresentDescSets;

    // UI overlay pipeline
    VkDescriptorSetLayout mUiDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout mUiPipelineLayout = VK_NULL_HANDLE;
    VkPipeline mUiPipeline = VK_NULL_HANDLE;
    VkSampler mUiFontSampler = VK_NULL_HANDLE;
    VkTexture mUiFontTexture;
    VkDescriptorSet mUiDescSet = VK_NULL_HANDLE;

    // Rotating frame resources
    FrameResource mFrameRes[2];
    int mFrameIndex = 0;

    // Caches
    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> mPipelines;
    std::unordered_map<uint64_t, VkSampler> mSamplers;
    std::unordered_map<SamplerDescKey, SamplerSetEntry, SamplerDescKeyHash> mSamplerDescSets;
    std::unordered_map<uint64_t, VkCopyTex> mCopies;

    VkCopyTex& getCopyTex(uint64_t key, int w, int h);
    void evictCopies(uint64_t currentFrame);

    VkTexture mDummyTexture;
    VkSampler mDummySampler = VK_NULL_HANDLE;

    // Frame command batching
    Builder mBuilder;
    std::unique_ptr<Prepared> mPrepared;
    std::shared_ptr<const Frame> mCurrentFrame;
    uint64_t mFrameNumber = 0;

    std::recursive_mutex mRenderMutex;
};

VkRenderer& vkRenderer();
void setVkPipelineCachePath(const char* path);

}  // namespace gpu
