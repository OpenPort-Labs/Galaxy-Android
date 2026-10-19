#pragma once

#include <sys/stat.h>
#include <unordered_map>
#include <vector>
#include <string.h>

#include "gpu.h"
#include "gx_record.h"
#include "gx_vertex_loader.h"
#include "port/heap_routing.h"
#include "port/port.h"
#include "tex_decode.h"

namespace gpu {

u64 hashRange(const u8* p, size_t n);
inline u64 hashBytes(const u8* p, size_t n) { return hashRange(p, n); }

static const u8 kTexRegBase[8] = {0x80, 0x81, 0x82, 0x83, 0xA0, 0xA1, 0xA2, 0xA3};

struct TexKey {
    u32 addr, fmt, width, height, levels, tlutAddr, tlutFmt;
    bool operator==(const TexKey& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
};

struct TexKeyHash {
    size_t operator()(const TexKey& k) const { return (size_t)hashBytes((const u8*)&k, sizeof(k)); }
};

struct TexEntry {
    std::shared_ptr<TexImage> image;
    u64 dataHash = 0;
    u64 checkedFrame = ~0ull;
};

struct EfbCopyInfo {
    u32 id;
    u32 width, height, format;
    u64 frame;
    u64 memHash;
};

struct VtxState {
    u32 vcdLo, vcdHi, matIndexA, matIndexB;
    u32 vat[8][3];
    u32 arrayBase[12], arrayStride[12];
    void capture();
    bool operator==(const VtxState& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
};

struct ArrayDep {
    u32 addr, bytes;
    u64 hash;
};

struct DlDraw {
    u32 prim, word, count, flags;
};

struct DlEntry {
    std::vector<u8> bytes;
    VtxState state;
    WordBuffer words;
    std::vector<DlDraw> draws;
    std::vector<ArrayDep> deps;
    u64 lastUsed = 0;
    bool valid = false;
    bool drawsOnly = false;
};

struct Recorder final : Backend {
    std::unique_ptr<Frame> frame{new Frame()};
    u64 frameNumber = 1;
    u8 tlutMem[0x80000];
    std::unordered_map<TexKey, TexEntry, TexKeyHash> texCache;
    std::unordered_map<const TexImage*, u32> frameTexIndex;
    std::unordered_map<u32, EfbCopyInfo> efbCopies;
    u32 nextCopyId = 1;
    u32 boundTex[8];
    bool texDirty = true;

    struct UnitMemo {
        u32 mode0, mode1, image0, image3, tlut, binding;
        u64 epoch = 0;
    };
    UnitMemo unitMemo[8];
    u64 bindEpoch = 1;
    std::vector<u32> decodeScratch;

    struct DlSlot {
        std::vector<DlEntry> ways;
        u64 noDrawsUntil = 0;
    };
    std::unordered_map<u64, DlSlot> dlCache;
    bool dlCacheOn = getenv("PETARI_NODLCACHE") == nullptr;
    bool dlCheck = getenv("PETARI_DLCHECK") != nullptr;
    WordBuffer dlCheckScratch;
    u64 dlCheckDraws = 0, dlCheckBad = 0;
    DlSlot* dlSlot = nullptr;
    DlEntry* dlEntry = nullptr;
    bool dlReplay = false;
    u32 dlDrawIndex = 0;
    ArrayUse dlUse;
    std::unordered_map<u64, u64> flushHashes;
    u64 flushHashesStream = ~0ull;
    struct DlStats {
        u64 calls = 0, hits = 0, recorded = 0, hashedBytes = 0, copiedWords = 0, decodedWords = 0;
    } dlStats;

    Recorder();

    void snapshot();
    void resetBindings();
    WordBuffer& cmds() { return frame->cmds; }
    u32* appendCmds(size_t n);
    u64 arrayHash(u32 addr, u32 bytes);
    bool depsCurrent(const DlEntry& e);

    bool displayListBegin(u32 addr, const u8* data, u32 size) override;
    void replayDraws(const DlEntry& e);
    void noteListLoad();
    void displayListEnd() override;
    u32 drawVertices(int prim, int vat, u32 count, const u8* data, u32 vertexStride);

    u32 usedTexMaps() const;
    void resolveTextures();
    u32 resolveUnit(u32 unit);
    void decode(TexImage& img, const u8* src, const TexKey& key);

    void draw(int primitive, int vat, u32 count, const u8* data, u32 vertexStride) override;
    void bpWrite(u32 reg, u32 value, u32) override;
    void xfWrite(u32 addr, u32 count) override;
    void cpWrite(u32 reg, u32) override;
    void efbCopy(bool toXfb, bool clear) override;
    void loadTlut(u32 regValue) override;
    void preloadTexture() override {}
    u32 peekColor(u16, u16) override { return 0; }
    u32 peekZ(u16, u16) override { return 0x00FFFFFF; }
    void endFrame();
};

extern Recorder* sRecorder;

}  // namespace gpu
