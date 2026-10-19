#include "gl_shadergen_common.h"

namespace gpu {

const char* kUniformBlocksVs = R"(
layout(binding = 8) uniform highp sampler2D s_rows;
layout(binding = 9) uniform highp usampler2D s_recs;
layout(binding = 10) uniform highp sampler2D s_st;
layout(binding = 11) uniform highp usampler2D s_vtxRec;
layout(std140, binding = 2) uniform EyeBlock {
    vec4 vrView[4];
    vec4 vrProj[4];
    ivec4 vrFlags;
    vec4 vrFocus;
    vec4 vrCut;
    vec4 vrEyePos;
    vec4 vrStereo;
    vec4 vrStereo2;
};

#define fetch_rows(r) texelFetch(s_rows, ivec2(int(r) & 2047, int(r) >> 11), 0)
#define fetch_recs(idx) texelFetch(s_recs, ivec2(int(idx) & 2047, int(idx) >> 11), 0).r
#define fetch_st(idx) texelFetch(s_st, ivec2(int(idx) & 2047, int(idx) >> 11), 0)
#define fetch_vtxRec(idx) texelFetch(s_vtxRec, ivec2(int(idx) & 2047, int(idx) >> 11), 0).r

#define slotRow(rb, firstSlot, idx) (fetch_recs((rb) + (firstSlot) + uint((idx) / 3)) + uint((idx) % 3))
#define mulRows(r, v) vec3(dot(fetch_rows(r), v), dot(fetch_rows((r) + 1u), v), dot(fetch_rows((r) + 2u), v))
#define L(i) fetch_st(lbase + uint(i))
)";

const char* kUniformBlocksFs = R"(
layout(binding = 10) uniform highp sampler2D s_st;
layout(std140, binding = 2) uniform EyeBlock {
    vec4 vrView[4];
    vec4 vrProj[4];
    ivec4 vrFlags;
    vec4 vrFocus;
    vec4 vrCut;
    vec4 vrEyePos;
    vec4 vrStereo;
    vec4 vrStereo2;
};

#define fetch_st(idx) texelFetch(s_st, ivec2(int(idx) & 2047, int(idx) >> 11), 0)
)";

const char* kColorIn[16] = {"prev.rgb", "prev.aaa", "c0.rgb", "c0.aaa", "c1.rgb", "c1.aaa", "c2.rgb", "c2.aaa",
                            "tex.rgb",  "tex.aaa",  "ras.rgb", "ras.aaa", "ivec3(255)", "ivec3(128)", "kc", "ivec3(0)"};
const char* kAlphaIn[8] = {"prev.a", "c0.a", "c1.a", "c2.a", "tex.a", "ras.a", "ka", "0"};
const char* kDest[4] = {"prev", "c0", "c1", "c2"};
const int kFrac[8] = {255, 223, 191, 159, 128, 96, 64, 32};
const char kComp[4] = {'r', 'g', 'b', 'a'};

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

std::string glKonstColor(uint32_t sel) {
    char buf[64];
    if (sel < 8) {
        snprintf(buf, sizeof(buf), "ivec3(%d)", kFrac[sel]);
    } else if (sel >= 12 && sel < 16) {
        snprintf(buf, sizeof(buf), "konst[%u].rgb", sel - 12);
    } else if (sel >= 16) {
        snprintf(buf, sizeof(buf), "konst[%u].%c%c%c", (sel - 16) & 3, kComp[(sel - 16) >> 2], kComp[(sel - 16) >> 2], kComp[(sel - 16) >> 2]);
    } else {
        snprintf(buf, sizeof(buf), "ivec3(0)");
    }
    return buf;
}

std::string glKonstAlpha(uint32_t sel) {
    char buf[64];
    if (sel < 8) {
        snprintf(buf, sizeof(buf), "%d", kFrac[sel]);
    } else if (sel >= 16) {
        snprintf(buf, sizeof(buf), "konst[%u].%c", (sel - 16) & 3, kComp[(sel - 16) >> 2]);
    } else {
        snprintf(buf, sizeof(buf), "0");
    }
    return buf;
}

std::string glTevRegular(const char* comp, uint32_t bias, uint32_t sub, uint32_t scale, bool alpha) {
    static const char* biasStr[3] = {"", " + 128", " - 128"};
    static const char* scaleLeft[4] = {"", " * 2", " * 4", ""};
    static const char* scaleRight[4] = {"", "", "", " / 2"};
    const char* lerpBias = ((scale == 3) == alpha) ? (sub ? " + 127" : " + 128") : "";
    char buf[512];
    snprintf(buf, sizeof(buf),
             "((((tevin_d.%s%s)%s) %c (((((tevin_a.%s * 256) + (tevin_b.%s - tevin_a.%s) * (tevin_c.%s + (tevin_c.%s / 128)))%s)%s) / 256))%s)", comp,
             biasStr[bias], scaleLeft[scale], sub ? '-' : '+', comp, comp, comp, comp, comp, scaleLeft[scale], lerpBias, scaleRight[scale]);
    return buf;
}

std::string glSwapString(const ShaderUid& u, uint32_t table) {
    uint32_t lo = u.ksel[table * 2], hi = u.ksel[table * 2 + 1];
    if ((lo & 0xFu) == 0 && (hi & 0xFu) == 0) {
        switch (table & 3) {
        case 0: return ".rgba";
        case 1: return ".rrra";
        case 2: return ".ggga";
        case 3: return ".bbba";
        }
    }
    std::string s = ".";
    s += kComp[lo & 3];
    s += kComp[(lo >> 2) & 3];
    s += kComp[hi & 3];
    s += kComp[(hi >> 2) & 3];
    return s;
}

const char* glAlphaTest(uint32_t func, const char* ref) {
    static char buf[4][96];
    static int slot;
    char* b = buf[slot++ & 3];
    switch (func) {
    case 0: return "false";
    case 1: snprintf(b, 96, "(a < %s)", ref); return b;
    case 2: snprintf(b, 96, "(a == %s)", ref); return b;
    case 3: snprintf(b, 96, "(a <= %s)", ref); return b;
    case 4: snprintf(b, 96, "(a > %s)", ref); return b;
    case 5: snprintf(b, 96, "(a != %s)", ref); return b;
    case 6: snprintf(b, 96, "(a >= %s)", ref); return b;
    default: return "true";
    }
}

}  // namespace gpu
