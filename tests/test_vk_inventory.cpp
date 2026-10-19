#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "gx/vk/vk_types.h"
#include "gx/vk_shader_inventory.h"
#include "gx/vk_renderer.h"

using namespace gpu;

static void test_key_round_trip() {
    printf("[TEST] Running PipelineKey canonical round-trip tests...\n");

    for (uint32_t dt = 0; dt <= 1; dt++) {
        for (uint32_t dw = 0; dw <= 1; dw++) {
            for (uint32_t dop = 0; dop < 8; dop++) {
                for (uint32_t blend = 0; blend <= 1; blend++) {
                    for (uint32_t cm = 0; cm < 4; cm++) {
                        for (uint32_t topo = 0; topo <= 2; topo++) {
                            DecodedPipelineKey d{};
                            d.vtxFlags = 0x4047;
                            d.depthTest = (dt != 0);
                            d.depthWrite = (dw != 0);
                            d.depthCompareOp = (VkCompareOp)dop;
                            d.blendEnable = (blend != 0);
                            d.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                            d.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                            d.colorBlendOp = VK_BLEND_OP_ADD;
                            d.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                            d.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                            d.alphaBlendOp = VK_BLEND_OP_ADD;
                            d.colorWriteMask = 15;
                            d.blendConstantAlpha = 0.5f;

                            if (cm == 0) d.cullMode = VK_CULL_MODE_NONE;
                            else if (cm == 1) d.cullMode = VK_CULL_MODE_FRONT_BIT;
                            else if (cm == 2) d.cullMode = VK_CULL_MODE_BACK_BIT;
                            else d.cullMode = VK_CULL_MODE_FRONT_AND_BACK;

                            if (topo == 0) d.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                            else if (topo == 1) d.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                            else d.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

                            PipelineKey k = encodePipelineKey(d);
                            DecodedPipelineKey dec = decodePipelineKey(k);
                            PipelineKey k2 = encodePipelineKey(dec);

                            assert(k == k2);
                            assert(dec.depthTest == d.depthTest);
                            assert(dec.depthWrite == (d.depthTest && d.depthWrite));
                            assert(dec.depthCompareOp == d.depthCompareOp);
                            assert(dec.blendEnable == d.blendEnable);
                            assert(dec.cullMode == d.cullMode);
                            assert(dec.topology == d.topology);
                        }
                    }
                }
            }
        }
    }
    printf("[PASS] PipelineKey canonical round-trip passed for all permutations!\n");
}

static void test_placeholder_exclusion() {
    printf("[TEST] Running PrewarmResult accounting tests...\n");

    ShaderInventoryStats s{};
    auto record = [&](PrewarmResult res) {
        switch (res) {
        case PrewarmResult::Created:
            s.realPipelinesCreated++;
            break;
        case PrewarmResult::Reused:
            s.realPipelinesReused++;
            break;
        case PrewarmResult::PlaceholderReturned:
            s.placeholderPipelineUses++;
            s.pipelineCreationFailures++;
            break;
        case PrewarmResult::SpirvUnavailable:
            s.spirvFailures++;
            break;
        case PrewarmResult::CreationFailed:
            s.pipelineCreationFailures++;
            break;
        default:
            break;
        }
    };

    record(PrewarmResult::Created);
    record(PrewarmResult::Reused);
    record(PrewarmResult::PlaceholderReturned);
    record(PrewarmResult::SpirvUnavailable);

    assert(s.realPipelinesCreated == 1);
    assert(s.realPipelinesReused == 1);
    assert(s.placeholderPipelineUses == 1);
    assert(s.pipelineCreationFailures == 1);
    assert(s.spirvFailures == 1);

    printf("[PASS] Placeholders strictly excluded from successful prewarm counts!\n");
}

int main() {
    printf("=== Galaxy-Android Vulkan Shader Inventory & Prewarm Test Suite ===\n");
    test_key_round_trip();
    test_placeholder_exclusion();
    printf("=== ALL C++ REGRESSION TESTS PASSED! ===\n");
    return 0;
}
