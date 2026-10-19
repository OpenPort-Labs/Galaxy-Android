#include "gl_shadergen_common.h"

namespace gpu {

std::string genFragmentShader(const ShaderUid& u) {
    ShaderGenOut o;
    o.w("#version 320 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n");
    o.s += kUniformBlocksFs;
    for (int i = 0; i < 8; i++) {
        o.w("layout(binding = %d) uniform highp sampler2D s_tex%d;\n", i, i);
    }
    o.w("flat in uint v_pix;\n");
    o.w("in vec4 v_clr0;\nin vec4 v_clr1;\n");
    for (uint32_t i = 0; i < u.numTexGens; i++) o.w("in vec3 v_tex%u;\n", i);
    o.w("in float v_viewz;\nin float v_gxz;\nout vec4 o_color;\n");
    o.w("vec2 projUv(vec3 t) { if (isnan(t.z) || isinf(t.z) || abs(t.z) < 1e-6) return (any(isnan(t.xy)) || any(isinf(t.xy))) ? vec2(0.0) : t.xy; vec2 r = t.xy / t.z; return (any(isnan(r)) || any(isinf(r))) ? vec2(0.0) : r; }\n");
    o.w("int idot(ivec3 a, ivec3 b) { ivec3 t = a * b; return t.x + t.y + t.z; }\n");
    if (u.cutaway) {
        o.w("in vec3 v_eye;\n");
        o.w("const float kBayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);\n");
    }

    o.w("void main() {\n");
    if (u.cutaway) {
        o.w("  if (vrFocus.w > 0.0) {\n");
        o.w("    float len = length(vrFocus.xyz);\n");
        o.w("    float along = clamp(dot(v_eye, vrFocus.xyz) / len, 0.0, len);\n");
        o.w("    float d = length(v_eye - vrFocus.xyz * (along / len));\n");
        o.w("    float radius = mix(vrCut.x, vrCut.y, along / len);\n");
        o.w("    float cut = (1.0 - smoothstep(radius * 0.7, radius, d)) * (1.0 - smoothstep(len - vrCut.z - 0.06, len - vrCut.z, along));\n");
        o.w("    cut = max(cut, 1.0 - smoothstep(vrCut.w * 0.6, vrCut.w, length(v_eye)));\n");
        o.w("    ivec2 q = ivec2(gl_FragCoord.xy) & 3;\n");
        o.w("    if (cut > (kBayer[q.y * 4 + q.x] + 0.5) / 16.0) discard;\n");
        o.w("  }\n");
    }
    o.w("  vec4 tevReg[4] = vec4[4](fetch_st(v_pix), fetch_st(v_pix + 1u), fetch_st(v_pix + 2u), fetch_st(v_pix + 3u));\n");
    o.w("  ivec4 konst[4];\n  for (int i = 0; i < 4; i++) konst[i] = ivec4(round(clamp(fetch_st(v_pix + 4u + uint(i)), 0.0, 1.0) * 255.0));\n");
    o.w("  vec4 alphaRef = fetch_st(v_pix + 8u);\n  vec4 fogColor = fetch_st(v_pix + 9u);\n  vec4 fogParams = fetch_st(v_pix + 10u);\n");
    o.w("  vec4 fogRange = fetch_st(v_pix + 11u);\n");
    o.w("  vec4 texSize[8];\n  for (int i = 0; i < 8; i++) texSize[i] = fetch_st(v_pix + 12u + uint(i));\n");
    o.w("  vec4 indMtx[6];\n  for (int i = 0; i < 6; i++) indMtx[i] = fetch_st(v_pix + 20u + uint(i));\n");
    o.w("  ivec4 prev = ivec4(round(tevReg[0] * 255.0));\n  ivec4 c0 = ivec4(round(tevReg[1] * 255.0));\n");
    o.w("  ivec4 c1 = ivec4(round(tevReg[2] * 255.0));\n  ivec4 c2 = ivec4(round(tevReg[3] * 255.0));\n");
    o.w("  ivec4 tex = ivec4(0);\n  ivec4 ras = ivec4(0);\n  ivec3 kc = ivec3(0);\n  int ka = 0;\n");
    o.w("  ivec4 tevin_a, tevin_b, tevin_c, tevin_d;\n");
    o.w("  const ivec3 comp16 = ivec3(1, 256, 0);\n  const ivec3 comp24 = ivec3(1, 256, 65536);\n");
    if (u.zTex) {
        o.w("  ivec4 ztex = ivec4(0);\n");
    }

    for (uint32_t s = 0; s < u.numIndStages; s++) {
        uint32_t map = (u.iref >> (s * 6)) & 7, coord = (u.iref >> (s * 6 + 3)) & 7;
        uint32_t scale = (u.indScale[s / 2] >> ((s & 1) * 8)) & 0xFF;
        uint32_t scaleS = scale & 15, scaleT = (scale >> 4) & 15;
        if (coord < u.numTexGens) {
            o.w("  vec3 ind%u = floor(texture(s_tex%u, projUv(v_tex%u) / vec2(%.1f, %.1f)).abg * 255.0 + 0.5);\n", s, map, coord,
                (double)(1u << (scaleS & 15)), (double)(1u << (scaleT & 15)));
        } else {
            o.w("  vec3 ind%u = vec3(0.0);\n", s);
        }
    }

    uint32_t lastColorDest = 0, lastAlphaDest = 0;
    for (uint32_t s = 0; s < u.numTevStages; s++) {
        uint32_t order = (u.tevOrder[s / 2] >> ((s & 1) * 12)) & 0xFFF;
        uint32_t map = order & 7, coord = (order >> 3) & 7, en = (order >> 6) & 1, chan = (order >> 7) & 7;
        uint32_t cc = u.tevColor[s], ac = u.tevAlpha[s];
        uint32_t ksel = u.ksel[s / 2] >> ((s & 1) * 10);
        uint32_t kcsel = (ksel >> 4) & 31, kasel = (ksel >> 9) & 31;
        uint32_t rswap = ac & 3, tswap = (ac >> 2) & 3;
        o.w("  // stage %u\n", s);

        std::string uv = "vec2(0.0)";
        if (coord < u.numTexGens) {
            char b[64];
            snprintf(b, sizeof(b), "projUv(v_tex%u)", coord);
            uv = b;
        }
        uint32_t ind = u.indCmd[s];
        if (u.numIndStages && ind) {
            uint32_t bt = ind & 3, bias = (ind >> 4) & 7, mtx = (ind >> 9) & 15;
            if (bt < u.numIndStages && mtx != 0) {
                const char* bs = (bias & 1) ? "128.0" : "0.0";
                const char* bt2 = (bias & 2) ? "128.0" : "0.0";
                const char* bu = (bias & 4) ? "128.0" : "0.0";
                char sExpr[256];
                char tExpr[256];
                sExpr[0] = '\0';
                tExpr[0] = '\0';
                if (mtx >= 1 && mtx <= 3) {
                    uint32_t m = mtx - 1;
                    snprintf(sExpr, sizeof(sExpr), "dot(indMtx[%u].xyz, ind%u - vec3(%s, %s, %s))", m * 2, bt, bs, bt2, bu);
                    snprintf(tExpr, sizeof(tExpr), "dot(indMtx[%u].xyz, ind%u - vec3(%s, %s, %s))", m * 2 + 1, bt, bs, bt2, bu);
                } else if (mtx >= 5 && mtx <= 7) {
                    uint32_t m = mtx - 5;
                    snprintf(sExpr, sizeof(sExpr), "dot(indMtx[%u].xyz, ind%u - vec3(%s, %s, %s))", m * 2, bt, bs, bt2, bu);
                    snprintf(tExpr, sizeof(tExpr), "0.0");
                } else if (mtx >= 9 && mtx <= 11) {
                    uint32_t m = mtx - 9;
                    snprintf(sExpr, sizeof(sExpr), "0.0");
                    snprintf(tExpr, sizeof(tExpr), "dot(indMtx[%u].xyz, ind%u - vec3(%s, %s, %s))", m * 2 + 1, bt, bs, bt2, bu);
                }
                if (sExpr[0] != '\0') {
                    char b[512];
                    snprintf(b, sizeof(b), "(%s + vec2(%s, %s) * texSize[%u].zw)", uv.c_str(), sExpr, tExpr, map);
                    uv = b;
                }
            }
        }
        if (en) {
            o.w("  vec4 sampleTex%u = texture(s_tex%u, %s)%s;\n", s, map, uv.c_str(), glSwapString(u, tswap).c_str());
            o.w("  vec4 safeTex%u = (any(isnan(sampleTex%u)) || any(isinf(sampleTex%u))) ? vec4(0.0) : clamp(sampleTex%u, 0.0, 1.0);\n", s, s, s, s);
            o.w("  tex = ivec4(round(safeTex%u * 255.0));\n", s);
            if (u.zTex) {
                o.w("  ztex = tex;\n");
            }
        } else {
            o.w("  tex = ivec4(0);\n");
        }
        const char* rasSrc = (chan == 0 || chan == 2 || chan == 4) ? "v_clr0" : (chan == 1 || chan == 3 || chan == 5) ? "v_clr1" : "vec4(0.0)";
        o.w("  vec4 safeRas%u = (any(isnan(%s)) || any(isinf(%s))) ? vec4(0.0) : clamp(%s, 0.0, 1.0);\n", s, rasSrc, rasSrc, rasSrc);
        o.w("  ras = ivec4(round(safeRas%u%s * 255.0));\n", s, glSwapString(u, rswap).c_str());
        o.w("  kc = %s;\n  ka = %s;\n", glKonstColor(kcsel).c_str(), glKonstAlpha(kasel).c_str());
        {
            uint32_t ca = (cc >> 12) & 15, cb = (cc >> 8) & 15, cc2 = (cc >> 4) & 15, cd = cc & 15;
            uint32_t aa = (ac >> 13) & 7, ab = (ac >> 10) & 7, ac2 = (ac >> 7) & 7, ad = (ac >> 4) & 7;
            o.w("  tevin_a = ivec4(clamp(%s, ivec3(0), ivec3(255)), clamp(%s, 0, 255));\n", kColorIn[ca], kAlphaIn[aa]);
            o.w("  tevin_b = ivec4(clamp(%s, ivec3(0), ivec3(255)), clamp(%s, 0, 255));\n", kColorIn[cb], kAlphaIn[ab]);
            o.w("  tevin_c = ivec4(clamp(%s, ivec3(0), ivec3(255)), clamp(%s, 0, 255));\n", kColorIn[cc2], kAlphaIn[ac2]);
            o.w("  tevin_d = ivec4(clamp(%s, ivec3(-1024), ivec3(1023)), clamp(%s, -1024, 1023));\n", kColorIn[cd], kAlphaIn[ad]);
        }

        // Color combiner.
        {
            uint32_t bias = (cc >> 16) & 3, sub = (cc >> 18) & 1, clampv = (cc >> 19) & 1, scale = (cc >> 20) & 3, dest = (cc >> 22) & 3;
            std::string expr;
            if (bias != 3) {
                expr = glTevRegular("rgb", bias, sub, scale, false);
            } else {
                static const char* cmp[8] = {
                    "((tevin_a.r > tevin_b.r) ? tevin_c.rgb : ivec3(0))",
                    "((tevin_a.r == tevin_b.r) ? tevin_c.rgb : ivec3(0))",
                    "((idot(tevin_a.rgb, comp16) > idot(tevin_b.rgb, comp16)) ? tevin_c.rgb : ivec3(0))",
                    "((idot(tevin_a.rgb, comp16) == idot(tevin_b.rgb, comp16)) ? tevin_c.rgb : ivec3(0))",
                    "((idot(tevin_a.rgb, comp24) > idot(tevin_b.rgb, comp24)) ? tevin_c.rgb : ivec3(0))",
                    "((idot(tevin_a.rgb, comp24) == idot(tevin_b.rgb, comp24)) ? tevin_c.rgb : ivec3(0))",
                    "(max(sign(tevin_a.rgb - tevin_b.rgb), ivec3(0)) * tevin_c.rgb)",
                    "((ivec3(1) - sign(abs(tevin_a.rgb - tevin_b.rgb))) * tevin_c.rgb)",
                };
                expr = std::string("tevin_d.rgb + ") + cmp[(scale << 1) | sub];
            }
            o.w("  %s.rgb = clamp(%s, ivec3(%s), ivec3(%s));\n", kDest[dest], expr.c_str(), clampv ? "0" : "-1024", clampv ? "255" : "1023");
            if (s == u.numTevStages - 1) {
                lastColorDest = dest;
            }
        }
        // Alpha combiner.
        {
            uint32_t bias = (ac >> 16) & 3, sub = (ac >> 18) & 1, clampv = (ac >> 19) & 1, scale = (ac >> 20) & 3, dest = (ac >> 22) & 3;
            std::string expr;
            if (bias != 3) {
                expr = glTevRegular("a", bias, sub, scale, true);
            } else {
                static const char* cmp[8] = {
                    "((tevin_a.r > tevin_b.r) ? tevin_c.a : 0)",
                    "((tevin_a.r == tevin_b.r) ? tevin_c.a : 0)",
                    "((idot(tevin_a.rgb, comp16) > idot(tevin_b.rgb, comp16)) ? tevin_c.a : 0)",
                    "((idot(tevin_a.rgb, comp16) == idot(tevin_b.rgb, comp16)) ? tevin_c.a : 0)",
                    "((idot(tevin_a.rgb, comp24) > idot(tevin_b.rgb, comp24)) ? tevin_c.a : 0)",
                    "((idot(tevin_a.rgb, comp24) == idot(tevin_b.rgb, comp24)) ? tevin_c.a : 0)",
                    "((tevin_a.a > tevin_b.a) ? tevin_c.a : 0)",
                    "((tevin_a.a == tevin_b.a) ? tevin_c.a : 0)",
                };
                expr = std::string("tevin_d.a + ") + cmp[(scale << 1) | sub];
            }
            o.w("  %s.a = clamp(%s, %s, %s);\n", kDest[dest], expr.c_str(), clampv ? "0" : "-1024", clampv ? "255" : "1023");
            if (s == u.numTevStages - 1) {
                lastAlphaDest = dest;
            }
        }
    }

    o.w("  ivec4 outi = ivec4(clamp(%s.rgb, ivec3(0), ivec3(255)), int(uint(%s.a) & 255u));\n", kDest[lastColorDest], kDest[lastAlphaDest]);
    o.w("  vec4 outc = vec4(outi) / 255.0;\n");
    // Alpha test.
    {
        uint32_t f0 = u.alphaFunc & 7, f1 = (u.alphaFunc >> 3) & 7, logic = (u.alphaFunc >> 6) & 3;
        if (!(f0 == 7 && f1 == 7)) {
            o.w("  float a = float(outi.a);\n");
            const char* t0 = glAlphaTest(f0, "alphaRef.x");
            const char* t1 = glAlphaTest(f1, "alphaRef.y");
            const char* ops[4] = {"&&", "||", "!=", "=="};
            o.w("  if (!((%s) %s (%s))) discard;\n", t0, ops[logic], t1);
        }
    }
    // Z texture.
    if (u.zTex) {
        uint32_t type = u.zTex & 3, op = (u.zTex >> 2) & 3;
        o.w("  {\n    ivec4 zt = ztex;\n");
        if (type == 0) {
            o.w("    int zv = zt.a;\n");
        } else if (type == 1) {
            o.w("    int zv = zt.r + zt.a * 256;\n");
        } else {
            o.w("    int zv = zt.r * 65536 + zt.g * 256 + zt.b;\n");
        }
        o.w("    zv += int(alphaRef.z);\n");
        if (op == 1) {
            o.w("    zv += int(gl_FragCoord.z * 16777215.0 + 0.5);\n");
        }
        o.w("    gl_FragDepth = float(zv & 0xFFFFFF) / 16777215.0;\n  }\n");
    }
    // Fog.
    uint32_t fogType = (u.fogSel >> 1) & 7, fogOrtho = u.fogSel & 1;
    if (fogType >= 2) {
        o.w("  {\n    float zc = floor(clamp(v_gxz, 0.0, 1.0) * 16777215.0);\n");
        if (fogOrtho) {
            o.w("    float ze = fogParams.x * zc / 16777216.0;\n");
        } else {
            o.w("    float denom = fogParams.z - floor(zc / exp2(clamp(fogParams.w, 0.0, 31.0)));\n");
            o.w("    float ze = (denom > 1.0) ? (fogParams.x * 16777216.0) / denom : 0.0;\n");
        }
        o.w("    float fz = clamp(ze - fogParams.y, 0.0, 1.0);\n");
        switch (fogType) {
        case 4:
            o.w("    fz = 1.0 - exp2(-8.0 * fz);\n");
            break;
        case 5:
            o.w("    fz = 1.0 - exp2(-8.0 * fz * fz);\n");
            break;
        case 6:
            o.w("    fz = exp2(-8.0 * (1.0 - fz));\n");
            break;
        case 7:
            o.w("    fz = exp2(-8.0 * (1.0 - fz) * (1.0 - fz));\n");
            break;
        default:
            break;
        }
        o.w("    outc.rgb = mix(outc.rgb, fogColor.rgb, fz);\n  }\n");
    }
    o.w("  o_color = clamp(outc, 0.0, 1.0);\n");
    const char* vis = debugEnv("PETARI_GLVIS");
    if (vis && u.numTexGens > 0) {
        if (!strcmp(vis, "uv")) {
            o.w("  o_color = vec4(fract(projUv(v_tex0)), 0.0, 1.0);\n");
        } else if (!strcmp(vis, "red")) {
            o.w("  o_color = vec4(1.0, 0.0, 0.0, 1.0);\n");
        } else if (!strcmp(vis, "tex")) {
            o.w("  o_color = vec4(texture(s_tex0, projUv(v_tex0)).rgb, 1.0);\n");
        }
    }
    o.w("}\n");
    return o.s;
}

}  // namespace gpu
