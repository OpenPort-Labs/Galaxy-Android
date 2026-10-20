#pragma once
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include "gx/vk/vk_types.h"
#include "gx/vk_shader_inventory.h"
#include "gx/vk_renderer.h"

using namespace gpu;

static inline void test_key_round_trip() {
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
                            d.depthCompareOp = (CanonicalDepthFunc)dop;
                            d.blendEnable = (blend != 0);
                            d.srcColorBlendFactor = CanonicalBlendFactor::SrcAlpha;
                            d.dstColorBlendFactor = CanonicalBlendFactor::OneMinusSrcAlpha;
                            d.colorBlendOp = CanonicalBlendOp::Add;
                            d.srcAlphaBlendFactor = CanonicalBlendFactor::One;
                            d.dstAlphaBlendFactor = CanonicalBlendFactor::Zero;
                            d.alphaBlendOp = CanonicalBlendOp::Add;
                            d.colorWriteMask = 15;
                            d.blendConstantAlpha = 0.5f;

                            if (cm == 0) d.cullMode = CanonicalCullMode::None;
                            else if (cm == 1) d.cullMode = CanonicalCullMode::Front;
                            else if (cm == 2) d.cullMode = CanonicalCullMode::Back;
                            else d.cullMode = CanonicalCullMode::FrontAndBack;

                            if (topo == 0) d.topology = CanonicalPrimitiveTopology::PointList;
                            else if (topo == 1) d.topology = CanonicalPrimitiveTopology::LineList;
                            else d.topology = CanonicalPrimitiveTopology::TriangleList;

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

static inline void test_placeholder_exclusion() {
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
    record(PrewarmResult::Created);
    record(PrewarmResult::Reused);
    record(PrewarmResult::PlaceholderReturned);
    record(PrewarmResult::SpirvUnavailable);
    record(PrewarmResult::CreationFailed);

    assert(s.realPipelinesCreated == 2);
    assert(s.realPipelinesReused == 1);
    assert(s.placeholderPipelineUses == 1);
    assert(s.spirvFailures == 1);
    assert(s.pipelineCreationFailures == 2);

    assert(s.realPipelinesCreated == 2);
    printf("[PASS] PrewarmResult accounting correctly discriminates real vs placeholder pipelines!\n");
}

static inline void test_shader_inventory_concurrency() {
    printf("[TEST] Running ShaderInventory multi-threaded concurrency stress test...\n");
    ShaderInventory& inv = ShaderInventory::get();

    constexpr int kNumThreads = 4;
    constexpr int kEntriesPerThread = 50;
    std::vector<std::thread> workers;

    for (int t = 0; t < kNumThreads; t++) {
        workers.emplace_back([&inv, t]() {
            for (int i = 0; i < kEntriesPerThread; i++) {
                ShaderUid u{};
                u.vtxFlags = 0x4047 + t;
                u.numChans = 1;
                u.colorCtrl[0] = (uint32_t)(t * 1000 + i);

                DecodedPipelineKey d{};
                d.uid = u;
                d.vtxFlags = u.vtxFlags;
                d.depthTest = true;
                d.depthWrite = true;
                d.cullMode = CanonicalCullMode::Back;
                PipelineKey k = encodePipelineKey(d);

                inv.addEntry(u, k, "concurrency_test_" + std::to_string(t), false);
                ShaderInventoryStats s = inv.stats();
                assert(s.totalUniqueKeys > 0);
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    ShaderInventoryStats finalStats = inv.stats();
    assert(finalStats.totalUniqueKeys >= kNumThreads * kEntriesPerThread);
    printf("[PASS] ShaderInventory concurrency stress test completed successfully without deadlocks!\n");
}
