#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstring>
#include <cmath>

#include "gpu.h"
#include "gx_record.h"
#include "gl_shadergen.h"

namespace gpu {

#ifndef _CAMERA_INFO_DEFINED_
#define _CAMERA_INFO_DEFINED_
struct CameraInfo {
    bool valid = false;
    float view[12];
    float pos[3];
    float watch[3];
    float up[3];
    float watchUp[3];
    float fovy;
    uint32_t flags;
    float aspect;
    float player[3];
    float playerUp[3];
};
#endif

enum ItemType : u8 { IT_DRAW, IT_COPY, IT_MARKER };

// IT_DRAW is a batch: consecutive GX draws with identical state.
struct Item {
    ItemType type;
    // Batch
    u32 uid;           // index into Prepared::uids
    uint32_t program;  // resolved by the render thread (GL program or Vulkan pipeline)
    uint32_t prim;     // GL_TRIANGLES, GL_LINES or GL_POINTS (or corresponding Vulkan primitive)
    u8 useVr, hud, hudSafe;
    u8 player;         // between the player markers (SpaceWarp stencil tag)
    u32 flags;
    u32 firstWord;     // first vertex, as a word offset into the frame's vertex data
    u32 endWord;       // end of the last merged draw's vertices
    u32 firstOrdinal;  // first vertex ordinal (record index stream)
    u32 idxOffset, idxCount;
    u32 draws;         // GX draws in the batch
    u32 texMask;       // texture units the program samples
    // State
    u8 blend, colorMask, depthTest, depthWrite, cull, dstAlpha;
    uint32_t srcRGB, dstRGB, eq, depthFunc, cullFace;
    float dstAlphaValue;
    float vp[4];  // x, y, w, h in native EFB coordinates (top-left origin)
    int sc[4];    // scissor left, top, right, bottom (native EFB)
    u32 tex[8];   // TexImage index, or 0x80000000 | EFB copy address >> 5; ~0u unused
    u32 texMode0[8], texMode1[8];
    // Copy: BP 0x49..0x52 values plus PE state at that point.
    u32 copy[10];
    u32 copyPixFmt, copyCmode, copyZmode;
    u32 marker;
};

bool sameBatchState(const Item& a, const Item& b);

inline u64 copyKey(u32 slot, u32 addr) { return ((u64)slot << 32) | (addr >> 5); }
inline const u64 kXfbCopyKey = ~0ull;

// A frame turned into batches and shader data. Built from a recording on any thread;
// uploaded and replayed by the render thread.
struct Prepared {
    std::shared_ptr<const Frame> frame;
    u64 number = 0;
    std::vector<Item> items;
    std::vector<ShaderUid> uids;
    std::vector<uint32_t> cutProgs;  // cutaway variants of uids' programs, 0 until resolved
    std::vector<float> rows;    // vec4 matrix rows
    std::vector<u32> recs;      // kRecWords per draw record
    std::vector<float> states;  // vec4 light / projection / pixel states
    std::vector<u32> vtxRec;    // draw record per vertex ordinal
    std::vector<u32> indices;
    CameraInfo cam;
    u32 clearAR = 0, clearGB = 0, clearZ = 0xFFFFFF;
    u32 nativeW = 640, nativeH = 456;
};

void logItems(const Prepared& p);

// Replays a recording's register writes to build a Prepared.
struct Builder {
    std::vector<Item> items;
    std::vector<ShaderUid> uids;
    std::vector<float> rows, states;
    std::vector<u32> recs, vtxRec, indices;
    CameraInfo cam;
    u32 clearAR = 0, clearGB = 0, clearZ = 0xFFFFFF;
    u32 nativeW = 640, nativeH = 456;

    // Shadows of the GPU registers.
    u32 bp[256];
    u32 tevReg[8], tevKonst[8];
    u32 xfRegs[256];
    float xfMem[0x1000];
    u32 boundTex[8];
    bool inHud = false;
    bool inSky = false;     // between sky markers: drawn around the game camera
    bool inPlayer = false;  // between player markers
    bool inPointer = false; // between pointer markers: the pointer's 2D cursor

    bool uidDirty = true;
    u32 lastUid = 0, lastUidFlags = ~0u;
    bool lastUidHud = false;

    bool drawStateDirty = true;
    bool lastDrawReusable = false;
    u32 lastDrawFlags = 0, lastDrawRec = 0;
    uint32_t lastDrawPrim = 0;

    std::unordered_map<u64, std::vector<u32>> stateIndex;  // content hash -> bases
    std::unordered_map<ShaderUid, u32, ShaderUidHash> uidIndex;
    u32 posSlot[22], nrmSlot[11], postSlot[64];
    u32 lightBase = ~0u, projBase = ~0u, pixBase = ~0u;

    void fillLights(LightState& b);
    void fillProj(ProjState& b);
    void fillPix(PixelState& b);
    u32 appendRows(const float* src, int n, int stride);
    u32 addState(const float* data, size_t vec4s);
    void invalidateXf(u32 a0, u32 a1);
    u32 drawRecord(bool useVr, bool sky, bool pointer, bool dualTex, u32 numTexGens);
    u32 screenTexGens(const Frame& f, const Item& it, u32 first, const ShaderUid& uid);
    u32 uidFor(const ShaderUid& uid);
    void appendIndices(u32 kind, u32 base, u32 count);
    void emitDraw(u32 prim, u32 flags, u32 first, u32 count, u32& ordinal);
    void build(std::shared_ptr<const Frame> frame, Prepared& out);
};

inline float bitsToFloat(u32 v) {
    float f;
    memcpy(&f, &v, 4);
    return f;
}


}  // namespace gpu
