#include "gx_shader_uid.h"
#include <cstring>

namespace gpu {

ShaderUid makeShaderUid(const uint32_t* bp, const uint32_t* xf, uint32_t vtxFlags, bool hud) {
    ShaderUid u;
    memset(&u, 0, sizeof(u));
    u.vtxFlags = vtxFlags;
    u.numTexGens = xf[0x3F] & 15;
    u.numChans = xf[0x09] & 3;
    u.dualTex = xf[0x12] & 1;
    for (int i = 0; i < 2; i++) {
        u.colorCtrl[i] = i < (int)u.numChans ? (xf[0x0E + i] & 0x7FFF) : 0;
        u.alphaCtrl[i] = i < (int)u.numChans ? (xf[0x10 + i] & 0x7FFF) : 0;
    }
    for (uint32_t i = 0; i < u.numTexGens && i < 8; i++) {
        u.texMtxInfo[i] = xf[0x40 + i] & 0x3FFFF;
        u.postMtxInfo[i] = u.dualTex ? (xf[0x50 + i] & 0x100) : 0;
    }
    uint32_t gen = bp[0x00];
    u.numTevStages = ((gen >> 10) & 15) + 1;
    u.numIndStages = (gen >> 16) & 7;
    for (uint32_t s = 0; s < u.numTevStages; s++) {
        u.tevColor[s] = bp[0xC0 + 2 * s] & 0xFFFFFF;
        u.tevAlpha[s] = bp[0xC1 + 2 * s] & 0xFFFFFF;
    }
    for (uint32_t i = 0; i < (u.numTevStages + 1) / 2; i++) {
        u.tevOrder[i] = bp[0x28 + i] & 0xFFFFFF;
    }
    for (int i = 0; i < 8; i++) {
        u.ksel[i] = bp[0xF6 + i] & 0xFFFFFF;
    }
    u.alphaFunc = (bp[0xF3] >> 16) & 0xFF;
    u.fogSel = (bp[0xF1] >> 20) & 0xF;
    if (u.numIndStages) {
        for (uint32_t s = 0; s < u.numTevStages; s++) {
            u.indCmd[s] = bp[0x10 + s] & 0x1FFFFF;
        }
        u.iref = bp[0x27] & 0xFFFFFF;
        u.indScale[0] = bp[0x25] & 0xFF;
        u.indScale[1] = bp[0x26] & 0xFF;
    }
    uint32_t pixFmt = bp[0x43] & 7;
    u.efbHasAlpha = pixFmt == 1;
    u.hud = hud;
    u.zTex = ((bp[0xF5] >> 2) & 3) ? (bp[0xF5] & 0xF) : 0;
    return u;
}

}  // namespace gpu
