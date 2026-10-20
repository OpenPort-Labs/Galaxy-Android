#pragma once
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include "gx/vk/vk_types.h"

struct StagedRecord {
    uint32_t uid[90];
    uint32_t pipelineKey[5];
    uint8_t flags;
    std::string source;
};

static inline uint32_t test_fnv1a32(const void* data, size_t size, uint32_t seed = 2166136261u) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t h = seed;
    for (size_t i = 0; i < size; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

static inline bool validateAndParseManifestBytes(const uint8_t* data, size_t fileSize, std::vector<StagedRecord>& outCommitted) {
    outCommitted.clear();
    if (fileSize < 20) return false;

    uint32_t magic = *(uint32_t*)(data + 0);
    uint32_t schema = *(uint32_t*)(data + 4);
    uint32_t build = *(uint32_t*)(data + 8);
    uint32_t count = *(uint32_t*)(data + 12);
    uint32_t headerCrc = *(uint32_t*)(data + 16);

    if (magic != 0x55535450) return false;
    if (schema != 2) return false;
    if (build < 20261019) return false;

    uint32_t computedHeaderCrc = test_fnv1a32(data, 16);
    if (headerCrc != computedHeaderCrc) return false;

    std::vector<StagedRecord> staged;
    staged.reserve(count);
    size_t offset = 20;

    for (uint32_t i = 0; i < count; i++) {
        size_t recStart = offset;
        if (offset + 380 + 1 + 2 > fileSize) return false;

        offset += 380;
        uint8_t flags = data[offset];
        offset += 1;

        uint16_t strLen = *(const uint16_t*)(data + offset);
        offset += 2;
        if (offset + strLen + 4 > fileSize) return false;

        std::string source((const char*)(data + offset), strLen);
        offset += strLen;

        uint32_t recCrc = *(const uint32_t*)(data + offset);
        offset += 4;

        size_t payloadLen = offset - recStart - 4;
        uint32_t computedRecCrc = test_fnv1a32(data + recStart, payloadLen);
        if (recCrc != computedRecCrc) return false;

        StagedRecord rec{};
        rec.flags = flags;
        rec.source = std::move(source);
        staged.push_back(std::move(rec));
    }

    if (offset != fileSize) return false;
    outCommitted = std::move(staged);
    return true;
}

static inline void test_atomic_manifest_validation() {
    printf("[TEST] Running Atomic Manifest Validation & Rejection tests...\n");

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

        if (truncatePayload) buf.resize(buf.size() - 50);
        if (addTrailingBytes) {
            buf.push_back(0xFF);
            buf.push_back(0xAA);
        }
        return buf;
    };

    {
        std::vector<uint8_t> buf = buildManifest(5);
        std::vector<StagedRecord> committed;
        assert(validateAndParseManifestBytes(buf.data(), buf.size(), committed));
        assert(committed.size() == 5);
        assert(committed[0].source == "Model_0.bdl");
        assert(committed[4].source == "Model_4.bdl");
    }
    {
        std::vector<uint8_t> buf = buildManifest(3, 0x11223344);
        std::vector<StagedRecord> committed;
        assert(!validateAndParseManifestBytes(buf.data(), buf.size(), committed));
        assert(committed.empty());
    }
    {
        std::vector<uint8_t> buf = buildManifest(3, 0x55535450, 2, 20261019, true);
        std::vector<StagedRecord> committed;
        assert(!validateAndParseManifestBytes(buf.data(), buf.size(), committed));
        assert(committed.empty());
    }
    {
        std::vector<uint8_t> buf = buildManifest(3, 0x55535450, 2, 20261019, false, true);
        std::vector<StagedRecord> committed;
        assert(!validateAndParseManifestBytes(buf.data(), buf.size(), committed));
        assert(committed.empty());
    }
    {
        std::vector<uint8_t> buf = buildManifest(3, 0x55535450, 2, 20261019, false, false, true);
        std::vector<StagedRecord> committed;
        assert(!validateAndParseManifestBytes(buf.data(), buf.size(), committed));
        assert(committed.empty());
    }
    {
        std::vector<uint8_t> buf = buildManifest(3, 0x55535450, 2, 20261019, false, false, false, false, true);
        std::vector<StagedRecord> committed;
        assert(!validateAndParseManifestBytes(buf.data(), buf.size(), committed));
        assert(committed.empty());
    }
    {
        std::vector<uint8_t> buf = buildManifest(3, 0x55535450, 2, 20261019, false, false, false, true);
        std::vector<StagedRecord> committed;
        assert(!validateAndParseManifestBytes(buf.data(), buf.size(), committed));
        assert(committed.empty());
    }
    {
        std::vector<uint8_t> buf(15, 0);
        std::vector<StagedRecord> committed;
        assert(!validateAndParseManifestBytes(buf.data(), buf.size(), committed));
        assert(committed.empty());
    }

    printf("[PASS] Atomic manifest validation and rejection verified for all failure paths!\n");
}
