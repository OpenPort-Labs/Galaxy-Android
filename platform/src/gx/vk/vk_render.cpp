#include "gx/vk/vk_common.h"

namespace gpu {

bool VkRenderer::update() {
    std::lock_guard<std::recursive_mutex> lock(mRenderMutex);
    auto latest = frameQueue().latest();
    if (latest && (!mCurrentFrame || latest->number != mFrameNumber)) {
        mFrameNumber = latest->number;
        mCurrentFrame = std::move(latest);
        if (!mPrepared) {
            mPrepared = std::make_unique<Prepared>();
        }
        mBuilder.build(mCurrentFrame, *mPrepared);
        return true;
    }
    return false;
}

bool VkRenderer::render(VkTarget& target, const EyeView* eye, VkTarget* hud, HudMode hudMode) {
    (void)hud;
    std::lock_guard<std::recursive_mutex> lock(mRenderMutex);
    if (!mInitialized || mFatal || target.framebuffer == VK_NULL_HANDLE) {
        return false;
    }

    static const char* failAfterEnv = vkDebugValue("PETARI_VKFAILAFTER", "debug.petari.vkfailafter");
    if (failAfterEnv && mFrameNumber >= (uint64_t)atoll(failAfterEnv)) {
        markFatal("render", "PETARI_VKFAILAFTER (test hook)", 0);
        return false;
    }

    if (!mCurrentFrame) {
        update();
    }
    if (!mCurrentFrame || !mPrepared) {
        return false;
    }

    const Frame& f = *mCurrentFrame;
    Prepared& p = *mPrepared;
    const std::vector<Item>& items = p.items;
    if (items.empty()) {
        return false;
    }

    if (!waitInFlightSubmissions("render")) {
        return false;
    }

    flushDeferredDeletes();
    trimFrameCaches(mFrameNumber);

    for (auto& item : mStagingBuffersToFree) {
        if (item.buffer != VK_NULL_HANDLE) vkDestroyBuffer(mDevice, item.buffer, nullptr);
        if (item.memory != VK_NULL_HANDLE) vkFreeMemory(mDevice, item.memory, nullptr);
    }
    mStagingBuffersToFree.clear();

    drainCompiledShaders();

    mFrameIndex = (mFrameIndex + 1) % 2;
    FrameResource& fr = mFrameRes[mFrameIndex];

    bool uploadOk = true;
    uploadOk &= uploadDynamicBuffer(fr.vbo, fr.vboMem, fr.vboMapped, fr.vboCap, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, f.verts.data(), f.verts.size() * sizeof(uint32_t));
    uploadOk &= uploadDynamicBuffer(fr.ibo, fr.iboMem, fr.iboMapped, fr.iboCap, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, p.indices.data(), p.indices.size() * sizeof(uint32_t));
    uploadOk &= uploadDynamicBuffer(fr.rowBuf, fr.rowMem, fr.rowMapped, fr.rowCap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, p.rows.data(), p.rows.size() * sizeof(float));
    uploadOk &= uploadDynamicBuffer(fr.recBuf, fr.recMem, fr.recMapped, fr.recCap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, p.recs.data(), p.recs.size() * sizeof(uint32_t));
    uploadOk &= uploadDynamicBuffer(fr.stBuf, fr.stMem, fr.stMapped, fr.stCap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, p.states.data(), p.states.size() * sizeof(float));
    uploadOk &= uploadDynamicBuffer(fr.vtxRecBuf, fr.vtxRecMem, fr.vtxRecMapped, fr.vtxRecCap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, p.vtxRec.data(), p.vtxRec.size() * sizeof(uint32_t));

    EyeBlock eb{};
    bool vrEye = eye && !eye->flatStereo;
    if (vrEye) {
        for (int i = 0; i < 16; i++) {
            eb.vrView[i / 4][i % 4] = eye->view[i];
            eb.vrProj[i / 4][i % 4] = eye->proj[i];
        }
        eb.vrFlags[0] = 1;
        memcpy(eb.vrFocus, eye->focus, sizeof(eb.vrFocus));
        memcpy(eb.vrCut, eye->cut, sizeof(eb.vrCut));
        memcpy(eb.vrEyePos, eye->eyePos, sizeof(eb.vrEyePos));
    } else if (eye) {
        eb.vrFlags[0] = 2;
        memcpy(eb.vrStereo, eye->stereo, sizeof(eb.vrStereo));
        eb.vrStereo2[0] = eye->pointer[0];
        eb.vrStereo2[1] = eye->pointer[1];
    }
    uploadOk &= uploadDynamicBuffer(fr.eyeUbo, fr.eyeMem, fr.eyeMapped, fr.eyeCap, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &eb, sizeof(eb));
    if (!uploadOk) {
        LOGE("render: failed to upload one or more dynamic buffers");
        return false;
    }

    if (fr.set0 == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = mStaticDescPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &mStorageDescLayout;
        if (vkAllocateDescriptorSets(mDevice, &ai, &fr.set0) != VK_SUCCESS) {
            LOGE("render: failed to allocate the per-frame storage descriptor set");
            fr.set0 = VK_NULL_HANDLE;
            return false;
        }
    }

    VkDescriptorBufferInfo bInfo[5]{};
    bInfo[0] = {fr.rowBuf, 0, VK_WHOLE_SIZE};
    bInfo[1] = {fr.recBuf, 0, VK_WHOLE_SIZE};
    bInfo[2] = {fr.stBuf, 0, VK_WHOLE_SIZE};
    bInfo[3] = {fr.vtxRecBuf, 0, VK_WHOLE_SIZE};
    bInfo[4] = {fr.eyeUbo, 0, sizeof(EyeBlock)};

    VkWriteDescriptorSet writes[5]{};
    for (uint32_t i = 0; i < 4; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = fr.set0;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &bInfo[i];
    }
    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = fr.set0;
    writes[4].dstBinding = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[4].pBufferInfo = &bInfo[4];

    vkUpdateDescriptorSets(mDevice, 5, writes, 0, nullptr);

    vkResetCommandBuffer(mCommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult beginRes = vkBeginCommandBuffer(mCommandBuffer, &beginInfo);
    if (beginRes != VK_SUCCESS) {
        markFatal("render", "vkBeginCommandBuffer", (int)beginRes);
        return false;
    }

    uploadTextures(mCommandBuffer, f.textures);

    executeDrawPass(mCommandBuffer, f, p, target, hudMode, fr);

    VkResult endRes = vkEndCommandBuffer(mCommandBuffer);
    if (endRes != VK_SUCCESS) {
        markFatal("render", "vkEndCommandBuffer", (int)endRes);
        return false;
    }

    if (mRenderSignaledSem) {
        vkQueueWaitIdle(mQueue);
        mRenderSignaledSem = false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &mCommandBuffer;
    VkSemaphore signalSems[] = {mRenderToPresentSem};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSems;

    vkResetFences(mDevice, 1, &mRenderFence);
    VkResult subRes = vkQueueSubmit(mQueue, 1, &submitInfo, mRenderFence);
    if (subRes != VK_SUCCESS) {
        mRenderSignaledSem = false;
        markFatal("render", "vkQueueSubmit", (int)subRes);
        target.hasValidContent = false;
        return false;
    }

    mRenderFenceInFlight = true;
    mRenderSignaledSem = true;
    target.hasValidContent = true;
    target.colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    evictCopies(p.number);
    return true;
}

}  // namespace gpu
