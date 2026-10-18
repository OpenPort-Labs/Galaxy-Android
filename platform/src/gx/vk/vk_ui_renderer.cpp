#include "gx/vk/vk_common.h"

namespace gpu {

void VkRenderer::renderUiOverlay(int width, int height, VkCommandBuffer cmd) {
    if (mUiPipeline == VK_NULL_HANDLE || mUiDescSet == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE) return;

    std::vector<mobile_ui::UIVertex> verts;
    mobile_ui::UIOverlay::get().generateVertices(width, height, verts);
    if (verts.empty()) return;

    FrameResource& fr = mFrameRes[mFrameIndex];
    uploadDynamicBuffer(fr.uiVbo, fr.uiVboMem, fr.uiVboMapped, fr.uiVboCap,
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        verts.data(), verts.size() * sizeof(mobile_ui::UIVertex));

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = (float)width;
    vp.height = (float)height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {(uint32_t)width, (uint32_t)height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mUiPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mUiPipelineLayout, 0, 1, &mUiDescSet, 0, nullptr);

    float proj[16] = {
        2.0f / (float)width,  0.0f,                 0.0f, 0.0f,
        0.0f,                 2.0f / (float)height, 0.0f, 0.0f,
        0.0f,                 0.0f,                 1.0f, 0.0f,
        -1.0f,                -1.0f,                0.0f, 1.0f
    };
    vkCmdPushConstants(cmd, mUiPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(proj), proj);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &fr.uiVbo, &offset);

    vkCmdDraw(cmd, (uint32_t)verts.size(), 1, 0, 0);
}

}  // namespace gpu
