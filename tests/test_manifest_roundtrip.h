#pragma once
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gx/vk_shader_manifest.h"

// Exercises the production manifest reader (gpu::parseAssetManifest), not a
// reimplementation of the format, so schema/width drift cannot pass unnoticed.
static inline std::string manifestTempPath() {
    const char* candidates[] = {getenv("TMPDIR"), "/data/local/tmp", "."};
    for (const char* dir : candidates) {
        if (!dir || !dir[0]) continue;
        std::string path = std::string(dir) + "/galaxy_manifest_roundtrip.bin";
        FILE* f = fopen(path.c_str(), "wb");
        if (f) {
            fclose(f);
            return path;
        }
    }
    return std::string();
}

static inline void appendU32(std::vector<uint8_t>& buf, uint32_t v) {
    uint8_t bytes[4];
    memcpy(bytes, &v, 4);
    buf.insert(buf.end(), bytes, bytes + 4);
}

static inline std::vector<uint8_t> buildProductionManifest(
    const uint32_t* uid, const uint32_t* key, const std::string& source,
    uint32_t schema = gpu::kInventoryVersion, uint32_t build = gpu::kBuildVersion,
    bool trailingGarbage = false) {
    std::vector<uint8_t> buf;
    std::vector<uint8_t> header;
    appendU32(header, gpu::kInventoryMagic);
    appendU32(header, schema);
    appendU32(header, build);
    appendU32(header, 1);
    buf.insert(buf.end(), header.begin(), header.end());
    appendU32(buf, gpu::fnv1a32(header.data(), header.size()));

    size_t recStart = buf.size();
    for (size_t i = 0; i < gpu::kManifestUidWords; i++) appendU32(buf, uid[i]);
    for (int i = 0; i < 5; i++) appendU32(buf, key[i]);
    buf.push_back(0);
    uint16_t len = (uint16_t)source.size();
    buf.push_back((uint8_t)(len & 0xFF));
    buf.push_back((uint8_t)(len >> 8));
    buf.insert(buf.end(), source.begin(), source.end());
    appendU32(buf, gpu::fnv1a32(buf.data() + recStart, buf.size() - recStart));

    if (trailingGarbage) buf.push_back(0x7F);
    return buf;
}

static inline bool writeBytes(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    if (fclose(f) != 0) ok = false;
    return ok;
}

static inline void test_manifest_schema_roundtrip() {
    printf("[TEST] Running production manifest reader round-trip tests...\n");

    const std::string path = manifestTempPath();
    if (path.empty()) {
        printf("[SKIP] no writable temp location for manifest round-trip test\n");
        return;
    }

    // Every word distinct and non-zero, including the fields schema 2 dropped
    // (alphaFunc, fogSel, indCmd, iref, indScale, hud, zTex, cutaway,
    // screenTexGens).
    static_assert(gpu::kManifestUidWords == 99, "schema 4 serialises all 99 ShaderUid words");
    uint32_t uid[gpu::kManifestUidWords] = {0};
    for (size_t i = 0; i < gpu::kManifestUidWords; i++) uid[i] = 0xA5A50000u + (uint32_t)i;
    uid[74] = 0x0000000F;  // alphaFunc
    uid[75] = 0x00000003;  // fogSel
    uid[92] = 0x12345678;  // iref
    uid[93] = 0x00000011;  // indScale[0]
    uid[94] = 0x00000022;  // indScale[1]
    uid[95] = 1;           // hud
    uid[96] = 2;           // zTex
    uid[97] = 1;           // cutaway
    uid[98] = 0x00000007;  // screenTexGens
    const uint32_t key[5] = {0x4047, 0x13, 0x0000000A, 0x0000100F, 0x00000002};
    const std::string source = "Model.bdl:Material";

    {
        std::vector<uint8_t> data = buildProductionManifest(uid, key, source);
        assert(writeBytes(path, data));

        std::vector<gpu::ManifestStagedEntry> entries;
        uint32_t build = 0;
        assert(gpu::parseAssetManifest(path, entries, build));
        assert(entries.size() == 1);
        assert(build == gpu::kBuildVersion);
        assert(memcmp(&entries[0].uid, uid, sizeof(uid)) == 0);
        assert(entries[0].key.flags == key[0]);
        assert(entries[0].key.depthState == key[1]);
        assert(entries[0].key.blendState == key[2]);
        assert(entries[0].key.blendState2 == key[3]);
        assert(entries[0].key.rasterState == key[4]);
        assert(entries[0].source == "asset:" + source);
        // Explicitly check the fields that schema 2 truncated.
        assert(entries[0].uid.hud == 1);
        assert(entries[0].uid.zTex == 2);
        assert(entries[0].uid.cutaway == 1);
        assert(entries[0].uid.screenTexGens == 7);
        assert(entries[0].uid.fogSel == 3);
        assert(entries[0].uid.iref == 0x12345678u);
        assert(entries[0].uid.indScale[0] == 0x11 && entries[0].uid.indScale[1] == 0x22);
    }
    {
        // Older-schema records are shorter: they must be rejected outright.
        std::vector<uint8_t> data = buildProductionManifest(uid, key, source, 2, gpu::kBuildVersion);
        assert(writeBytes(path, data));
        std::vector<gpu::ManifestStagedEntry> entries;
        uint32_t build = 0;
        assert(!gpu::parseAssetManifest(path, entries, build));
        assert(entries.empty());
    }
    {
        std::vector<uint8_t> data = buildProductionManifest(uid, key, source, gpu::kInventoryVersion, gpu::kBuildVersion - 1);
        assert(writeBytes(path, data));
        std::vector<gpu::ManifestStagedEntry> entries;
        uint32_t build = 0;
        assert(!gpu::parseAssetManifest(path, entries, build));
        assert(entries.empty());
    }
    {
        std::vector<uint8_t> data = buildProductionManifest(uid, key, source, gpu::kInventoryVersion, gpu::kBuildVersion, true);
        assert(writeBytes(path, data));
        std::vector<gpu::ManifestStagedEntry> entries;
        uint32_t build = 0;
        assert(!gpu::parseAssetManifest(path, entries, build));
        assert(entries.empty());
    }
    {
        std::vector<uint8_t> data = buildProductionManifest(uid, key, source);
        data.resize(data.size() - 6);  // truncated record
        assert(writeBytes(path, data));
        std::vector<gpu::ManifestStagedEntry> entries;
        uint32_t build = 0;
        assert(!gpu::parseAssetManifest(path, entries, build));
        assert(entries.empty());
    }

    {
        // The inventory cache writes through the production writer and must be
        // readable again by the production reader (that is how runtime keys
        // survive into the next launch's prewarm pass).
        std::vector<gpu::ManifestStagedEntry> written(2);
        memcpy(&written[0].uid, uid, sizeof(written[0].uid));
        written[0].key.flags = 0x22;
        written[0].key.depthState = 0x13;
        written[0].key.blendState = 0x0A;
        written[0].key.blendState2 = 0x100F;
        written[0].key.rasterState = 0x02;
        written[0].source = "runtime_key_miss";
        written[1] = written[0];
        written[1].uid.hud = 1;
        written[1].key.flags = 0x2A;
        written[1].source = "asset:Model.bdl:Material";

        assert(gpu::writeAssetManifest(path, written, gpu::kBuildVersion));
        std::vector<gpu::ManifestStagedEntry> roundtrip;
        uint32_t build = 0;
        assert(gpu::parseAssetManifest(path, roundtrip, build));
        assert(build == gpu::kBuildVersion);
        assert(roundtrip.size() == 2);
        assert(memcmp(&roundtrip[0].uid, &written[0].uid, sizeof(written[0].uid)) == 0);
        assert(roundtrip[1].uid.hud == 1);
        assert(roundtrip[1].key.flags == 0x2A);
        assert(roundtrip[0].source == "asset:runtime_key_miss");
        assert(roundtrip[1].source == "asset:Model.bdl:Material");
    }

    remove(path.c_str());
    printf("[PASS] Production manifest reader/writer preserve every UID word and reject stale schemas!\n");
}
