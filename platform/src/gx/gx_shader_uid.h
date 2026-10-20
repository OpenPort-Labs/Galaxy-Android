#pragma once

#include <stdint.h>
#include <string.h>

namespace gpu {

struct ShaderUid {
    uint32_t vtxFlags;
    uint32_t numTexGens;
    uint32_t numChans;
    uint32_t dualTex;
    uint32_t colorCtrl[2];
    uint32_t alphaCtrl[2];
    uint32_t texMtxInfo[8];
    uint32_t postMtxInfo[8];
    uint32_t numTevStages;
    uint32_t numIndStages;
    uint32_t tevOrder[8];
    uint32_t tevColor[16];
    uint32_t tevAlpha[16];
    uint32_t ksel[8];
    uint32_t alphaFunc;   // BP 0xF3 bits 16-23
    uint32_t fogSel;      // BP 0xF1 bits 20-23
    uint32_t indCmd[16];
    uint32_t iref;
    uint32_t indScale[2];
    uint32_t hud;         // drawn into the HUD layer
    uint32_t zTex;        // BP 0xF5: Z texture type (bits 0-1) and op (bits 2-3)
    uint32_t cutaway;     // VR cutaway variant; has a discard
    uint32_t screenTexGens;  // bit n: texgen n maps positions to screen position

    bool operator==(const ShaderUid& o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
};

struct ShaderUidHash {
    size_t operator()(const ShaderUid& u) const {
        const uint32_t* w = (const uint32_t*)&u;
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < sizeof(u) / 4; i++) {
            h = (h ^ w[i]) * 1099511628211ull;
        }
        return (size_t)h;
    }
};

ShaderUid makeShaderUid(const uint32_t* bp, const uint32_t* xfRegs, uint32_t vtxFlags, bool hud);

}  // namespace gpu
