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

    // Every word distinct and non-zero, including the ten words schema 2 used
    // to drop (74..99): fogSel, indCmd, iref, indScale, efbHasAlpha, hud,
    // zTex, cutaway and screenTexGens.
    uint32_t uid[gpu::kManifestUidWords] = {0};
    for (size_t i = 0; i < gpu::kManifestUidWords; i++) uid[i] = 0xA5A50000u + (uint32_t)i;
    uid[74] = 0x0000000F;  // alphaFunc
    uid[75] = 0x00000003;  // fogSel
    uid[92] = 0x12345678;  // iref
    uid[93] = 0x00000011;  // indScale[0]
    uid[94] = 0x00000022;  // indScale[1]
    uid[95] = 1;           // efbHasAlpha
    uid[96] = 1;           // hud
    uid[97] = 2;           // zTex
    uid[98] = 1;           // cutaway
    uid[99] = 0x00000007;  // screenTexGens
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
        assert(entries[0].uid.efbHasAlpha == 1);
        assert(entries[0].uid.zTex == 2);
        assert(entries[0].uid.cutaway == 1);
        assert(entries[0].uid.screenTexGens == 7);
        assert(entries[0].uid.fogSel == 3);
        assert(entries[0].uid.iref == 0x12345678u);
        assert(entries[0].uid.indScale[0] == 0x11 && entries[0].uid.indScale[1] == 0x22);
    }
    {
        // Schema 2 records are 40 bytes shorter: they must be rejected outright.
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

    remove(path.c_str());
    printf("[PASS] Production manifest reader preserves all 100 UID words and rejects stale schemas!\n");
}
