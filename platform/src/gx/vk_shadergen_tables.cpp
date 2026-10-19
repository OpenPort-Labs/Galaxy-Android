#include "vk_shadergen_common.h"

namespace gpu {

const char* kVulkanUniformBlocksVs = R"(
layout(std430, set = 0, binding = 0) readonly buffer RowsBuf { vec4 b_rows[]; };
layout(std430, set = 0, binding = 1) readonly buffer RecsBuf { uint b_recs[]; };
layout(std430, set = 0, binding = 2) readonly buffer StatesBuf { vec4 b_st[]; };
layout(std430, set = 0, binding = 3) readonly buffer VtxRecBuf { uint b_vtxRec[]; };

layout(std140, set = 0, binding = 4) uniform EyeBlock {
    vec4 vrView[4];
    vec4 vrProj[4];
    ivec4 vrFlags;
    vec4 vrFocus;
    vec4 vrCut;
    vec4 vrEyePos;
    vec4 vrStereo;
    vec4 vrStereo2;
};

layout(push_constant) uniform PushConsts {
    uint u_firstOrdinal;
};

#define fetch_rows(r) b_rows[r]
#define fetch_recs(idx) b_recs[idx]
#define fetch_st(idx) b_st[idx]
#define fetch_vtxRec(idx) b_vtxRec[idx]

#define slotRow(rb, firstSlot, idx) (fetch_recs((rb) + (firstSlot) + uint((idx) / 3)) + uint((idx) % 3))
#define mulRows(r, v) vec3(dot(fetch_rows(r), v), dot(fetch_rows((r) + 1u), v), dot(fetch_rows((r) + 2u), v))
#define L(i) fetch_st(lbase + uint(i))
)";

const char* kVulkanUniformBlocksFs = R"(
layout(std430, set = 0, binding = 2) readonly buffer StatesBuf { vec4 b_st[]; };

layout(std140, set = 0, binding = 4) uniform EyeBlock {
    vec4 vrView[4];
    vec4 vrProj[4];
    ivec4 vrFlags;
    vec4 vrFocus;
    vec4 vrCut;
    vec4 vrEyePos;
    vec4 vrStereo;
    vec4 vrStereo2;
};

#define fetch_st(idx) b_st[idx]
)";

const char* kVkColorIn[16] = {"prev.rgb", "prev.aaa", "c0.rgb", "c0.aaa", "c1.rgb", "c1.aaa", "c2.rgb", "c2.aaa",
                              "tex.rgb",  "tex.aaa",  "ras.rgb", "ras.aaa", "ivec3(255)", "ivec3(128)", "kc", "ivec3(0)"};
const char* kVkAlphaIn[8] = {"prev.a", "c0.a", "c1.a", "c2.a", "tex.a", "ras.a", "ka", "0"};
const char* kVkDest[4] = {"prev", "c0", "c1", "c2"};
const int kVkFrac[8] = {255, 223, 191, 159, 128, 96, 64, 32};
const char kVkComp[4] = {'r', 'g', 'b', 'a'};

std::string vkKonstColor(uint32_t sel) {
    char buf[64];
    if (sel < 8) {
        snprintf(buf, sizeof(buf), "ivec3(%d)", kVkFrac[sel]);
    } else if (sel >= 12 && sel < 16) {
        snprintf(buf, sizeof(buf), "konst[%u].rgb", sel - 12);
    } else if (sel >= 16) {
        snprintf(buf, sizeof(buf), "konst[%u].%c%c%c", (sel - 16) & 3, kVkComp[(sel - 16) >> 2], kVkComp[(sel - 16) >> 2], kVkComp[(sel - 16) >> 2]);
    } else {
        snprintf(buf, sizeof(buf), "ivec3(0)");
    }
    return buf;
}

std::string vkKonstAlpha(uint32_t sel) {
    char buf[64];
    if (sel < 8) {
        snprintf(buf, sizeof(buf), "%d", kVkFrac[sel]);
    } else if (sel >= 16) {
        snprintf(buf, sizeof(buf), "konst[%u].%c", (sel - 16) & 3, kVkComp[(sel - 16) >> 2]);
    } else {
        snprintf(buf, sizeof(buf), "0");
    }
    return buf;
}

std::string vkTevRegular(const char* comp, uint32_t bias, uint32_t sub, uint32_t scale, bool alpha) {
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

std::string vkSwapString(const ShaderUid& u, uint32_t table) {
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
    s += kVkComp[lo & 3];
    s += kVkComp[(lo >> 2) & 3];
    s += kVkComp[hi & 3];
    s += kVkComp[(hi >> 2) & 3];
    return s;
}

const char* vkAlphaTest(uint32_t func, const char* ref) {
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
