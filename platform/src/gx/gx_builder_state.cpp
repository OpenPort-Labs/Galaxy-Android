#include "gx_builder.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace gpu {

static void rgba8ToVec(u32 c, float* out) {
    out[0] = ((c >> 24) & 255) / 255.0f;
    out[1] = ((c >> 16) & 255) / 255.0f;
    out[2] = ((c >> 8) & 255) / 255.0f;
    out[3] = (c & 255) / 255.0f;
}

static inline float s11(u32 v) {
    int x = (int)(v & 0x7FF);
    if (x & 0x400) x -= 0x800;
    return x / 255.0f;
}

static u64 hashWords(const u32* w, size_t n) {
    u64 h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ w[i]) * 1099511628211ull;
    return h;
}

bool sameBatchState(const Item& a, const Item& b) {
    if (a.uid != b.uid || a.prim != b.prim || a.useVr != b.useVr || a.hud != b.hud || a.hudSafe != b.hudSafe || a.player != b.player || a.flags != b.flags) return false;
    if (a.blend != b.blend || a.colorMask != b.colorMask || a.depthTest != b.depthTest || a.depthWrite != b.depthWrite || a.cull != b.cull ||
        a.dstAlpha != b.dstAlpha)
        return false;
    if ((a.blend || a.dstAlpha) && (a.srcRGB != b.srcRGB || a.dstRGB != b.dstRGB || a.eq != b.eq)) return false;
    if (a.dstAlpha && a.dstAlphaValue != b.dstAlphaValue) return false;
    if (a.depthTest && a.depthFunc != b.depthFunc) return false;
    if (a.cull && a.cullFace != b.cullFace) return false;
    if (memcmp(a.vp, b.vp, sizeof(a.vp)) != 0 || memcmp(a.sc, b.sc, sizeof(a.sc)) != 0) return false;
    if (a.texMask != b.texMask) return false;
    for (int t = 0; t < 8; t++) {
        if ((a.texMask & (1u << t)) && (a.tex[t] != b.tex[t] || a.texMode0[t] != b.texMode0[t] || a.texMode1[t] != b.texMode1[t])) return false;
    }
    return true;
}

void Builder::fillLights(LightState& b) {
    for (int l = 0; l < 8; l++) {
        const float* m = xfMem + 0x600 + l * 16;
        u32 col;
        memcpy(&col, &m[3], 4);
        rgba8ToVec(col, b.lights[l][0]);
        for (int k = 0; k < 3; k++) {
            b.lights[l][1][k] = m[4 + k];
            b.lights[l][2][k] = m[7 + k];
            b.lights[l][3][k] = m[10 + k];
            b.lights[l][4][k] = m[13 + k];
        }
        float lposLenSq = b.lights[l][3][0] * b.lights[l][3][0] + b.lights[l][3][1] * b.lights[l][3][1] + b.lights[l][3][2] * b.lights[l][3][2];
        if (lposLenSq > 1e10f) {
            float inv = 10000.0f / sqrtf(lposLenSq);
            b.lights[l][3][0] *= inv;
            b.lights[l][3][1] *= inv;
            b.lights[l][3][2] *= inv;
        }
        b.lights[l][1][3] = b.lights[l][2][3] = b.lights[l][3][3] = b.lights[l][4][3] = 0.0f;
    }
    rgba8ToVec(xfRegs[0x0C], b.matColor[0]);
    rgba8ToVec(xfRegs[0x0D], b.matColor[1]);
    rgba8ToVec(xfRegs[0x0A], b.ambColor[0]);
    rgba8ToVec(xfRegs[0x0B], b.ambColor[1]);
}

void Builder::fillProj(ProjState& b) {
    float p[6];
    for (int i = 0; i < 6; i++) p[i] = bitsToFloat(xfRegs[0x20 + i]);
    memset(b.proj, 0, sizeof(b.proj));
    if (xfRegs[0x26] & 1) {
        b.proj[0][0] = p[0];
        b.proj[0][3] = p[1];
        b.proj[1][1] = p[2];
        b.proj[1][3] = p[3];
        b.proj[2][2] = p[4];
        b.proj[2][3] = p[5];
        b.proj[3][3] = 1.0f;
    } else {
        b.proj[0][0] = p[0];
        b.proj[0][2] = p[1];
        b.proj[1][1] = p[2];
        b.proj[1][2] = p[3];
        b.proj[2][2] = p[4];
        b.proj[2][3] = p[5];
        b.proj[3][2] = -1.0f;
    }
    b.depthParams[0] = bitsToFloat(xfRegs[0x1C]);
    b.depthParams[1] = bitsToFloat(xfRegs[0x1F]);
    b.depthParams[2] = b.depthParams[3] = 0.0f;
}

void Builder::fillPix(PixelState& b) {
    for (int i = 0; i < 4; i++) {
        u32 lo = tevReg[i * 2], hi = tevReg[i * 2 + 1];
        b.tevReg[i][0] = s11(lo);
        b.tevReg[i][3] = s11(lo >> 12);
        b.tevReg[i][2] = s11(hi);
        b.tevReg[i][1] = s11(hi >> 12);
        u32 klo = tevKonst[i * 2], khi = tevKonst[i * 2 + 1];
        b.konst[i][0] = (klo & 0xFF) / 255.0f;
        b.konst[i][3] = ((klo >> 12) & 0xFF) / 255.0f;
        b.konst[i][2] = (khi & 0xFF) / 255.0f;
        b.konst[i][1] = ((khi >> 12) & 0xFF) / 255.0f;
    }
    b.alphaRef[0] = (float)(bp[0xF3] & 0xFF);
    b.alphaRef[1] = (float)((bp[0xF3] >> 8) & 0xFF);
    b.alphaRef[2] = (float)(bp[0xF4] & 0xFFFFFF);
    b.alphaRef[3] = 0.0f;
    u32 fc = bp[0xF2];
    b.fogColor[0] = ((fc >> 16) & 255) / 255.0f;
    b.fogColor[1] = ((fc >> 8) & 255) / 255.0f;
    b.fogColor[2] = (fc & 255) / 255.0f;
    b.fogColor[3] = 1.0f;
    auto fogFloat = [](u32 reg) {
        u32 bits = (reg & 0xFFFFF) << 12;
        float f;
        memcpy(&f, &bits, 4);
        return f;
    };
    b.fogParams[0] = fogFloat(bp[0xEE]);
    b.fogParams[1] = fogFloat(bp[0xF1]);
    b.fogParams[2] = (float)(bp[0xEF] & 0xFFFFFF);
    b.fogParams[3] = (float)(bp[0xF0] & 0x1F);
    memset(b.fogRange, 0, sizeof(b.fogRange));
    static const u8 base[8] = {0x88, 0x89, 0x8A, 0x8B, 0xA8, 0xA9, 0xAA, 0xAB};
    static const u8 mode0[8] = {0x80, 0x81, 0x82, 0x83, 0xA0, 0xA1, 0xA2, 0xA3};
    for (int i = 0; i < 8; i++) {
        u32 img = bp[base[i]];
        float w = (float)((img & 0x3FF) + 1), h = (float)(((img >> 10) & 0x3FF) + 1);
        int bias = (int)((bp[mode0[i]] >> 9) & 0xFF);
        b.texSize[i][0] = (float)(bias >= 0x80 ? bias - 0x100 : bias) / 32.0f;
        b.texSize[i][1] = h;
        b.texSize[i][2] = 1.0f / w;
        b.texSize[i][3] = 1.0f / h;
    }
    for (int m = 0; m < 3; m++) {
        u32 ra = bp[0x06 + m * 3], rb = bp[0x07 + m * 3], rc = bp[0x08 + m * 3];
        int scale = (int)(((ra >> 22) & 3) | (((rb >> 22) & 3) << 2) | (((rc >> 22) & 1) << 4)) - 17;
        float sc = ldexpf(1.0f, scale) / 1024.0f;
        auto sv = [](u32 v) {
            int x = (int)(v & 0x7FF);
            if (x & 0x400) x -= 0x800;
            return (float)x;
        };
        b.indMtx[m][0][0] = sv(ra) * sc;
        b.indMtx[m][0][1] = sv(rb) * sc;
        b.indMtx[m][0][2] = sv(rc) * sc;
        b.indMtx[m][0][3] = 0.0f;
        b.indMtx[m][1][0] = sv(ra >> 11) * sc;
        b.indMtx[m][1][1] = sv(rb >> 11) * sc;
        b.indMtx[m][1][2] = sv(rc >> 11) * sc;
        b.indMtx[m][1][3] = 0.0f;
    }
    b.efbSize[0] = (float)nativeW;
    b.efbSize[1] = (float)nativeH;
    b.efbSize[2] = b.efbSize[3] = 0.0f;
}

u32 Builder::appendRows(const float* src, int n, int stride) {
    u32 base = (u32)(rows.size() / 4);
    for (int r = 0; r < n; r++) {
        for (int k = 0; k < 4; k++) rows.push_back(k < stride ? src[r * stride + k] : 0.0f);
    }
    return base;
}

u32 Builder::addState(const float* data, size_t vec4s) {
    const u32* w = (const u32*)data;
    u64 h = hashWords(w, vec4s * 4) ^ vec4s;
    std::vector<u32>& bases = stateIndex[h];
    for (u32 b : bases) {
        if (memcmp(&states[(size_t)b * 4], data, vec4s * 16) == 0) return b;
    }
    u32 base = (u32)(states.size() / 4);
    states.insert(states.end(), data, data + vec4s * 4);
    bases.push_back(base);
    return base;
}

void Builder::invalidateXf(u32 a0, u32 a1) {
    if (a0 < 0x100) {
        u32 r0 = a0 / 4, r1 = ((a1 < 0x100 ? a1 : 0x100) - 1) / 4;
        for (u32 slot = r0 / 3; slot <= r1 / 3 && slot < 22; slot++) posSlot[slot] = ~0u;
    }
    if (a1 > 0x400 && a0 < 0x460) {
        u32 r0 = ((a0 > 0x400 ? a0 : 0x400) - 0x400) / 3, r1 = ((a1 < 0x460 ? a1 : 0x460) - 0x400 - 1) / 3;
        for (u32 slot = r0 / 3; slot <= r1 / 3 && slot < 11; slot++) nrmSlot[slot] = ~0u;
    }
    if (a1 > 0x500 && a0 < 0x600) {
        int r0 = (int)((a0 > 0x500 ? a0 : 0x500) - 0x500) / 4, r1 = (int)((a1 < 0x600 ? a1 : 0x600) - 0x500 - 1) / 4;
        for (int start = r0 - 2; start <= r1; start++) {
            if (start >= 0 && start < 64) postSlot[start] = ~0u;
        }
    }
    if (a1 > 0x600 && a0 < 0x680) lightBase = ~0u;
    if (a1 > 0x100A && a0 < 0x100E) lightBase = ~0u;
    if (a1 > 0x101C && a0 < 0x1027) projBase = ~0u;
}

u32 Builder::drawRecord(bool useVr, bool sky, bool pointer, bool dualTex, u32 numTexGens) {
    u32 rec[kRecWords];
    memset(rec, 0, sizeof(rec));
    for (int i = 0; i < 22; i++) {
        if (posSlot[i] == ~0u) posSlot[i] = appendRows(xfMem + i * 12, 3, 4);
        rec[kRecPosSlots + i] = posSlot[i];
    }
    for (int i = 0; i < 11; i++) {
        if (nrmSlot[i] == ~0u) nrmSlot[i] = appendRows(xfMem + 0x400 + i * 9, 3, 3);
        rec[kRecNrmSlots + i] = nrmSlot[i];
    }
    if (dualTex) {
        for (u32 i = 0; i < numTexGens && i < 8; i++) {
            u32 p = xfRegs[0x50 + i] & 63;
            if (postSlot[p] == ~0u) postSlot[p] = appendRows(xfMem + 0x500 + p * 4, 3, 4);
            rec[kRecPost + i] = postSlot[p];
        }
    }
    if (lightBase == ~0u) {
        LightState ls;
        fillLights(ls);
        lightBase = addState(&ls.lights[0][0][0], sizeof(ls) / 16);
    }
    if (projBase == ~0u) {
        ProjState ps;
        fillProj(ps);
        projBase = addState(&ps.proj[0][0], sizeof(ps) / 16);
    }
    if (pixBase == ~0u) {
        PixelState px;
        fillPix(px);
        pixBase = addState(&px.tevReg[0][0], sizeof(px) / 16);
    }
    rec[kRecLights] = lightBase;
    rec[kRecProj] = projBase;
    rec[kRecPixel] = pixBase;
    rec[kRecMtxIdxA] = xfRegs[0x18] ? xfRegs[0x18] : 0x27921780;
    rec[kRecMtxIdxB] = xfRegs[0x19] ? xfRegs[0x19] : 0x00cf0b6a;
    rec[kRecFlags] = (useVr ? 1u : 0u) | (sky ? 2u : 0u) | (pointer ? 4u : 0u);
    size_t n = recs.size();
    if (n >= kRecWords && memcmp(&recs[n - kRecWords], rec, sizeof(rec)) == 0) {
        return (u32)(n / kRecWords) - 1;
    }
    recs.insert(recs.end(), rec, rec + kRecWords);
    return (u32)(n / kRecWords);
}

}  // namespace gpu
