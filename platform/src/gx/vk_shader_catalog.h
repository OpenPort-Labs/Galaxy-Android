#pragma once

#include <cstdint>
#include "gx_shader_uid.h"
#include "gx/vk/vk_types.h"

namespace gpu {

class ShaderInventory;

PipelineKey makeCanonicalPipelineKey(const ShaderUid& uid, uint32_t vtxFlags, bool depthTest,
                                     bool depthWrite, VkCompareOp depthOp, bool blendEnable,
                                     VkBlendFactor srcColor, VkBlendFactor dstColor,
                                     VkBlendFactor srcAlpha, VkBlendFactor dstAlpha,
                                     VkCullModeFlags cullMode,
                                     VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

void populateCanonicalGalaxyCatalog(ShaderInventory& inv);

}  // namespace gpu
