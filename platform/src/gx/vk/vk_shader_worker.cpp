#include "gx/vk/vk_common.h"

namespace gpu {

PipelineKey pipelineKeyFor(const Item& it, const ShaderUid& uid);

VkShaderModule VkRenderer::createShaderModule(const uint32_t* code, size_t sizeBytes) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = sizeBytes;
    createInfo.pCode = code;

    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(mDevice, &createInfo, nullptr, &module);
    return module;
}

void VkRenderer::requestShader(const ShaderUid& uid, const Item& it) {
    mShaderPendingItems[uid] = it;
    {
        std::lock_guard<std::mutex> lock(mShaderMutex);
        if (mShaderStop || mShaderQueued.count(uid)) return;
        mShaderQueued.insert(uid);
        mShaderQueue.push_back(uid);
    }
    mShaderCv.notify_one();
}

void VkRenderer::startShaderWorker() {
    if (mShaderThread.joinable()) return;
    mShaderStop = false;
    mShaderThread = std::thread([this]() {
        for (;;) {
            ShaderUid uid;
            {
                std::unique_lock<std::mutex> lock(mShaderMutex);
                mShaderCv.wait(lock, [this]() { return mShaderStop || !mShaderQueue.empty(); });
                if (mShaderQueue.empty()) return;
                uid = mShaderQueue.front();
                mShaderQueue.pop_front();
            }

            CompiledShader result;
            result.uid = uid;
            auto start = std::chrono::steady_clock::now();
            result.ok = compileVulkanShader(uid, result.vsSpirv, result.fsSpirv);
            result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

            std::lock_guard<std::mutex> lock(mShaderMutex);
            mShaderQueued.erase(uid);
            mShaderDone.push_back(std::move(result));
        }
    });
}

void VkRenderer::stopShaderWorker() {
    {
        std::lock_guard<std::mutex> lock(mShaderMutex);
        mShaderStop = true;
        mShaderQueue.clear();
        mShaderQueued.clear();
        mShaderDone.clear();
    }
    mShaderCv.notify_all();
    if (mShaderThread.joinable()) mShaderThread.join();
    mShaderPendingItems.clear();
    mShaderFailed.clear();
}

void VkRenderer::drainCompiledShaders() {
    CompiledShader shader;
    auto drainStart = std::chrono::steady_clock::now();
    int createdCount = 0;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(mShaderMutex);
            if (mShaderDone.empty()) return;
            shader = std::move(mShaderDone.front());
            mShaderDone.erase(mShaderDone.begin());
        }

        auto pending = mShaderPendingItems.find(shader.uid);
        if (!shader.ok) {
            mShaderFailed.insert(shader.uid);
            LOGE("VkRenderer: shader program for draw %u could not be compiled; its draws stay hidden",
                 pending != mShaderPendingItems.end() ? pending->second.uid : 0u);
            if (pending != mShaderPendingItems.end()) mShaderPendingItems.erase(pending);
            continue;
        }

        mShaderCompiles++;
        if (pending == mShaderPendingItems.end()) continue;
        const Item it = pending->second;
        mShaderPendingItems.erase(pending);
        LOGI("VkRenderer: compiled program for draw %u off the render thread in %.0f ms", it.uid, shader.ms);
        createPipeline(it, pipelineKeyFor(it, shader.uid), shader.uid, shader.vsSpirv, shader.fsSpirv);
        createdCount++;
        auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - drainStart).count();
        if (createdCount >= 4 || elapsed >= 5.0) {
            return;
        }
    }
}

}  // namespace gpu
