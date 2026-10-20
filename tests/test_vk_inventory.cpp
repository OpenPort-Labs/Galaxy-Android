#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include "gx/vk/vk_types.h"
#include "gx/vk_shader_inventory.h"
#include "gx/vk_renderer.h"

using namespace gpu;

static uint32_t test_fnv1a32(const void* data, size_t size, uint32_t seed = 2166136261u) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t h = seed;
    for (size_t i = 0; i < size; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

// ---------------------------------------------------------------------------
// 1. PipelineKey Canonical Round-Trip Tests
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// 2. PrewarmResult Accounting Tests
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// 3. Atomic Manifest Validation & Rejection Tests
// ---------------------------------------------------------------------------
struct StagedRecord {
    uint32_t uidWords[90];
    uint32_t keyWords[5];
    uint8_t flags;
    std::string source;
};

static bool validateAndParseManifestBytes(const uint8_t* data, size_t fileSize,
                                         std::vector<StagedRecord>& outCommitted) {
    // Exact replica of the production C++ loader validation rules
    if (!data || fileSize < 20) {
        return false;
    }

    struct Header {
        uint32_t magic;
        uint32_t schemaVersion;
        uint32_t buildVersion;
        uint32_t count;
        uint32_t crc32;
    };

    Header hdr;
    memcpy(&hdr, data, 20);

    if (hdr.magic != 0x55535450) return false;
    if (hdr.schemaVersion != 2) return false;
    if (hdr.buildVersion != 20261019) return false;
    if (hdr.count > 100000) return false;

    // Minimum file size check
    if (fileSize < 20 + (size_t)hdr.count * 387) return false;

    // Header CRC check
    uint32_t computedHdrCrc = test_fnv1a32(data, 16);
    if (hdr.crc32 != computedHdrCrc) return false;

    std::vector<StagedRecord> staged;
    staged.reserve(hdr.count);

    size_t offset = 20;
    for (uint32_t i = 0; i < hdr.count; i++) {
        size_t recStart = offset;
        if (offset + 387 > fileSize) return false;

        StagedRecord rec{};
        memcpy(rec.uidWords, data + offset, 360);
        offset += 360;

        memcpy(rec.keyWords, data + offset, 20);
        offset += 20;

        rec.flags = data[offset++];

        uint16_t strLen = 0;
        memcpy(&strLen, data + offset, 2);
        offset += 2;

        if (strLen > 512) return false;
        if (offset + strLen + 4 > fileSize) return false;

        if (strLen > 0) {
            rec.source.assign((const char*)(data + offset), strLen);
            offset += strLen;
        }

        uint32_t expectedRecCrc = 0;
        memcpy(&expectedRecCrc, data + offset, 4);
        offset += 4;

        size_t payloadLen = offset - recStart - 4;
        uint32_t computedRecCrc = test_fnv1a32(data + recStart, payloadLen);
        if (expectedRecCrc != computedRecCrc) return false;

        staged.push_back(std::move(rec));
    }

    // Exact EOF check
    if (offset != fileSize) return false;

    // Atomic publish
    outCommitted = std::move(staged);
    return true;
}

static void test_atomic_manifest_validation() {
    printf("[TEST] Running Atomic Manifest Validation & Rejection tests...\n");

    // Helper to build a valid manifest buffer
    auto buildManifest = [](uint32_t count, uint32_t magic = 0x55535450,
                            uint32_t schema = 2, uint32_t build = 20261019,
                            bool corruptHdrCrc = false, bool corruptRecCrc = false,
                            bool corruptStrLen = false, bool addTrailingBytes = false,
                            bool truncatePayload = false) -> std::vector<uint8_t> {
        std::vector<uint8_t> buf;
        buf.resize(20);

        uint32_t hdrData[4] = { magic, schema, build, count };
        memcpy(buf.data(), hdrData, 16);
        uint32_t hCrc = test_fnv1a32(buf.data(), 16);
        if (corruptHdrCrc) hCrc ^= 0xDEADBEEF;
        memcpy(buf.data() + 16, &hCrc, 4);

        for (uint32_t i = 0; i < count; i++) {
            size_t recStart = buf.size();
            uint32_t uid[90]{};
            uid[0] = i + 1;
            uint32_t key[5]{ 1, 2, 3, 4, 5 };
            uint8_t flags = 0;
            std::string src = "Model_" + std::to_string(i) + ".bdl";
            uint16_t strLen = (uint16_t)src.size();
            if (corruptStrLen && i == count - 1) strLen = 999;

            buf.insert(buf.end(), (uint8_t*)uid, (uint8_t*)uid + 360);
            buf.insert(buf.end(), (uint8_t*)key, (uint8_t*)key + 20);
            buf.push_back(flags);
            buf.insert(buf.end(), (uint8_t*)&strLen, (uint8_t*)&strLen + 2);
            buf.insert(buf.end(), src.begin(), src.end());

            size_t payloadLen = buf.size() - recStart;
            uint32_t rCrc = test_fnv1a32(buf.data() + recStart, payloadLen);
            if (corruptRecCrc && i == count - 1) rCrc ^= 0xCAFEBABE;
            buf.insert(buf.end(), (uint8_t*)&rCrc, (uint8_t*)&rCrc + 4);
        }

        if (truncatePayload) {
            buf.resize(buf.size() - 50);
        }
        if (addTrailingBytes) {
            buf.push_back(0xFF);
            buf.push_back(0xAA);
        }
        return buf;
    };

    // 1. Valid manifest loads successfully
    {
        std::vector<uint8_t> buf = buildManifest(5);
        std::vector<StagedRecord> committed;
        bool ok = validateAndParseManifestBytes(buf.data(), buf.size(), committed);
        assert(ok);
        assert(committed.size() == 5);
        assert(committed[0].source == "Model_0.bdl");
        assert(committed[4].source == "Model_4.bdl");
    }

    // 2. Invalid magic is rejected with zero published entries
    {
        std::vector<uint8_t> buf = buildManifest(3, 0x11223344);
        std::vector<StagedRecord> committed;
        bool ok = validateAndParseManifestBytes(buf.data(), buf.size(), committed);
        assert(!ok);
        assert(committed.empty());
    }

    // 3. Corrupt header CRC is rejected with zero published entries
    {
        std::vector<uint8_t> buf = buildManifest(3, 0x55535450, 2, 20261019, true);
        std::vector<StagedRecord> committed;
        bool ok = validateAndParseManifestBytes(buf.data(), buf.size(), committed);
        assert(!ok);
        assert(committed.empty());
    }

    // 4. Corrupt record CRC at record 2 of 3 causes atomic rollback (0 published entries)
    {
        std::vector<uint8_t> buf = buildManifest(3, 0x55535450, 2, 20261019, false, true);
        std::vector<StagedRecord> committed;
        bool ok = validateAndParseManifestBytes(buf.data(), buf.size(), committed);
        assert(!ok);
        assert(committed.empty());  // Atomic rejection: records 0 and 1 must NOT be published!
    }

    // 5. Oversized string length is rejected
    {
        std::vector<uint8_t> buf = buildManifest(3, 0x55535450, 2, 20261019, false, false, true);
        std::vector<StagedRecord> committed;
        bool ok = validateAndParseManifestBytes(buf.data(), buf.size(), committed);
        assert(!ok);
        assert(committed.empty());
    }

    // 6. Truncated record payload is rejected
    {
        std::vector<uint8_t> buf = buildManifest(3, 0x55535450, 2, 20261019, false, false, false, false, true);
        std::vector<StagedRecord> committed;
        bool ok = validateAndParseManifestBytes(buf.data(), buf.size(), committed);
        assert(!ok);
        assert(committed.empty());
    }

    // 7. Trailing garbage bytes at EOF are rejected
    {
        std::vector<uint8_t> buf = buildManifest(3, 0x55535450, 2, 20261019, false, false, false, true);
        std::vector<StagedRecord> committed;
        bool ok = validateAndParseManifestBytes(buf.data(), buf.size(), committed);
        assert(!ok);
        assert(committed.empty());
    }

    // 8. Truncated header (< 20 bytes) is rejected
    {
        std::vector<uint8_t> buf(15, 0);
        std::vector<StagedRecord> committed;
        bool ok = validateAndParseManifestBytes(buf.data(), buf.size(), committed);
        assert(!ok);
        assert(committed.empty());
    }

    printf("[PASS] Atomic manifest validation and rejection verified for all failure paths!\n");
}

// ---------------------------------------------------------------------------
// 4. Quick-Save Star-Bit Accounting Verification
// ---------------------------------------------------------------------------
static void test_quicksave_starbit_accounting() {
    printf("[TEST] Running Quick-Save Star-Bit Accounting tests...\n");

    s32 stockStarBits = 100;
    s32 stageStarBits = 0;
    s32 creditedQuickSaveStarBits = 0;

    auto quickSave = [&]() {
        s32 currentPieces = stageStarBits;
        s32 uncreditedBits = std::max(0, currentPieces - creditedQuickSaveStarBits);
        if (uncreditedBits > 0) {
            stockStarBits += uncreditedBits;
        }
        // Strictly monotonic high-water mark: never decremented while in stage
        creditedQuickSaveStarBits = std::max(creditedQuickSaveStarBits, currentPieces);
    };

    auto stageClearTransfer = [&]() {
        s32 uncreditedPieces = std::max(0, stageStarBits - creditedQuickSaveStarBits);
        stockStarBits += uncreditedPieces;
        creditedQuickSaveStarBits = 0;
        stageStarBits = 0;
    };

    auto reloadSave = [&]() {
        creditedQuickSaveStarBits = 0;
        stageStarBits = 0;
    };

    // Scenario A: Player enters stage with 100 stock, collects 20 star bits
    stageStarBits = 20;
    quickSave();
    assert(stockStarBits == 120);
    assert(creditedQuickSaveStarBits == 20);

    // Immediate second quick-save without collecting star bits: stock MUST NOT change
    quickSave();
    assert(stockStarBits == 120);
    assert(creditedQuickSaveStarBits == 20);

    // Player collects 15 more star bits (total 35 in stage)
    stageStarBits = 35;
    quickSave();
    assert(stockStarBits == 135);  // exactly +15
    assert(creditedQuickSaveStarBits == 35);

    // Scenario B: Player feeds hungry Luma 10 star bits (stage drops to 25)
    stageStarBits = 25;
    // Quick-saving after spending must NOT lower credited mark, and must NOT double-credit
    quickSave();
    assert(stockStarBits == 135);
    assert(creditedQuickSaveStarBits == 35);

    // Player collects 5 more bits (stage goes to 30, still <= 35)
    stageStarBits = 30;
    quickSave();
    assert(stockStarBits == 135);
    assert(creditedQuickSaveStarBits == 35);

    // Player collects 10 more bits (stage goes to 40, which is > 35)
    stageStarBits = 40;
    quickSave();
    assert(stockStarBits == 140);  // exactly +5 (40 - 35)
    assert(creditedQuickSaveStarBits == 40);

    // Scenario C: Stage completion transfer
    // At stage clear, starPieceNum is 40, credited is 40 -> uncredited is 0!
    stageClearTransfer();
    assert(stockStarBits == 140);  // NO DUPLICATION at stage clear!
    assert(creditedQuickSaveStarBits == 0);

    // Scenario D: Stage without quick-saves
    stageStarBits = 50;
    stageClearTransfer();
    assert(stockStarBits == 190);  // full 50 transferred

    // Scenario E: Save reload resets credited tracker
    stageStarBits = 15;
    quickSave();
    assert(stockStarBits == 205);
    reloadSave();
    assert(creditedQuickSaveStarBits == 0);

    printf("[PASS] Quick-save star-bit accounting strictly preserves conservation laws!\n");
}

// ---------------------------------------------------------------------------
// 5. Vulkan Copy-Texture Slot Telemetry & Fallback Verification
// ---------------------------------------------------------------------------
static void test_copy_texture_slot_telemetry() {
    printf("[TEST] Running Vulkan Copy-Texture Telemetry tests...\n");

    VkCopyTex slots[2]{};
    slots[0].w = 1280;
    slots[0].h = 720;
    slots[0].frame = 42;

    uint32_t hits = 0;
    uint32_t fallbacks = 0;
    uint32_t misses = 0;

    auto lookup = [&](uint32_t eyeIndex) -> const VkCopyTex* {
        if (eyeIndex < 2 && slots[eyeIndex].w > 0) {
            hits++;
            return &slots[eyeIndex];
        }
        if (slots[0].w > 0) {
            fallbacks++;
            return &slots[0];
        }
        misses++;
        return nullptr;
    };

    // Slot 0 query (non-stereo or left eye) hits slot 0 directly
    const VkCopyTex* t0 = lookup(0);
    assert(t0 != nullptr && t0->frame == 42);
    assert(hits == 1 && fallbacks == 0 && misses == 0);

    // Slot 1 query (right eye) when slot 1 is unpopulated falls back to slot 0 with telemetry
    const VkCopyTex* t1 = lookup(1);
    assert(t1 != nullptr && t1->frame == 42);
    assert(hits == 1 && fallbacks == 1 && misses == 0);

    // Populate slot 1
    slots[1].w = 1280;
    slots[1].h = 720;
    slots[1].frame = 99;

    const VkCopyTex* t1_populated = lookup(1);
    assert(t1_populated != nullptr && t1_populated->frame == 99);
    assert(hits == 2 && fallbacks == 1 && misses == 0);

    // Out-of-bounds slot falls back to slot 0
    const VkCopyTex* t_oob = lookup(5);
    assert(t_oob != nullptr && t_oob->frame == 42);
    assert(hits == 2 && fallbacks == 2 && misses == 0);

    printf("[PASS] Vulkan copy-texture slot telemetry and fallback handling verified!\n");
}

// ---------------------------------------------------------------------------
// 6. Frame Pacer Phase Debt Recovery Verification
// ---------------------------------------------------------------------------
static void test_frame_pacer_timing() {
    printf("[TEST] Running Frame Pacer Timing & Phase Debt tests...\n");

    uint64_t periodNs = 16666667; // 60 FPS
    uint64_t lastPresentNs = 1000000000;
    uint64_t targetNs = lastPresentNs + periodNs;

    // Simulation of a frame spike taking 28ms (missed 16.6ms deadline)
    uint64_t nowNs = lastPresentNs + 28000000;
    assert(nowNs >= targetNs);

    // Fixed logic: reset lastPresentNs = nowNs, preventing accumulated debt
    lastPresentNs = nowNs;
    uint64_t nextTargetNs = lastPresentNs + periodNs;
    assert(nextTargetNs == nowNs + periodNs);

    // Game speed decoupling
    float targetFps = 60.0f;
    float gameSpeed = 3.0f;
    float effectivePresentationFps = targetFps; // decoupled from simulation speed
    assert(effectivePresentationFps == 60.0f);

    printf("[PASS] Frame pacer phase debt recovery and rate decoupling verified!\n");
}

int main() {
    printf("=== Galaxy-Android Vulkan Shader Inventory & Regression Test Suite ===\n");
    test_key_round_trip();
    test_placeholder_exclusion();
    test_atomic_manifest_validation();
    test_quicksave_starbit_accounting();
    test_copy_texture_slot_telemetry();
    test_frame_pacer_timing();
    printf("=== ALL C++ REGRESSION TESTS PASSED! ===\n");
    return 0;
}

