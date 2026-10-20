#include "vk_shadergen_common.h"

namespace gpu {

std::string genVulkanVertexShader(const ShaderUid& u) {
    VulkanShaderGenOut o;
    o.w("#version 450\n");
    o.s += kVulkanUniformBlocksVs;
    uint32_t f = u.vtxFlags;
    o.w("layout(location = 0) in vec3 a_pos;\n");
    if (f & VF_POSMTX) o.w("layout(location = 1) in uint a_posmtx;\n");
    if (f & VF_NRM) o.w("layout(location = 2) in vec3 a_nrm;\n");
    if (f & VF_NBT) o.w("layout(location = 3) in vec3 a_bin;\nlayout(location = 4) in vec3 a_tan;\n");
    if (f & VF_CLR0) o.w("layout(location = 5) in vec4 a_clr0;\n");
    if (f & VF_CLR1) o.w("layout(location = 6) in vec4 a_clr1;\n");
    for (int i = 0; i < 8; i++) {
        if (f & (VF_TEX0 << i)) o.w("layout(location = %d) in vec2 a_tex%d;\n", 7 + i, i);
    }
    if (f & VF_TEXMTX) o.w("layout(location = 15) in uvec2 a_texmtx;\n");
    o.w("layout(location = 0) flat out uint v_pix;\n");
    o.w("layout(location = 1) out vec4 v_clr0;\nlayout(location = 2) out vec4 v_clr1;\n");
    for (uint32_t i = 0; i < u.numTexGens; i++) o.w("layout(location = %u) out vec3 v_tex%u;\n", 3 + i, i);
    o.w("layout(location = %u) out float v_viewz;\nlayout(location = %u) out float v_gxz;\n", 3 + u.numTexGens, 4 + u.numTexGens);
    if (u.cutaway) o.w("layout(location = %u) out vec3 v_eye;\n", 5 + u.numTexGens);

    o.w("void main() {\n");
    o.w("  uint rb = fetch_vtxRec(u_firstOrdinal + uint(gl_VertexIndex)) * %uu;\n", (unsigned)kRecWords);
    o.w("  uint lbase = fetch_recs(rb + %uu);\n  uint pjbase = fetch_recs(rb + %uu);\n", (unsigned)kRecLights, (unsigned)kRecProj);
    o.w("  v_pix = fetch_recs(rb + %uu);\n", (unsigned)kRecPixel);
    o.w("  uint mA = fetch_recs(rb + %uu);\n  uint mB = fetch_recs(rb + %uu);\n", (unsigned)kRecMtxIdxA, (unsigned)kRecMtxIdxB);
    o.w("  vec4 proj[4] = vec4[4](fetch_st(pjbase), fetch_st(pjbase + 1u), fetch_st(pjbase + 2u), fetch_st(pjbase + 3u));\n");
    o.w("  vec4 depthParams = fetch_st(pjbase + 4u);\n");
    o.w("  vec4 matColor[2] = vec4[2](L(40), L(41));\n  vec4 ambColor[2] = vec4[2](L(42), L(43));\n");
    if (f & VF_POSMTX) {
        o.w("  int pidx = int(a_posmtx);\n");
    } else {
        o.w("  int pidx = int(mA & 63u);\n");
    }
    o.w("  vec4 p4 = vec4(a_pos, 1.0);\n");
    o.w("  vec3 pos = mulRows(slotRow(rb, %uu, pidx), p4);\n", (unsigned)kRecPosSlots);
    o.w("  int nidx = pidx & 31;\n");
    o.w("  uint nr = slotRow(rb, %uu, nidx);\n", (unsigned)kRecNrmSlots);
    o.w("  vec3 nrm = vec3(0.0, 0.0, 1.0);\n  vec3 bin = vec3(0.0);\n  vec3 tan = vec3(0.0);\n");
    if (f & VF_NRM) {
        o.w("  nrm = vec3(dot(fetch_rows(nr).xyz, a_nrm), dot(fetch_rows(nr + 1u).xyz, a_nrm), dot(fetch_rows(nr + 2u).xyz, a_nrm));\n");
        o.w("  nrm = (dot(nrm, nrm) > 1e-6 && !any(isnan(nrm)) && !any(isinf(nrm))) ? normalize(nrm) : vec3(0.0, 0.0, 1.0);\n");
    }
    if (f & VF_NBT) {
        o.w("  bin = vec3(dot(fetch_rows(nr).xyz, a_bin), dot(fetch_rows(nr + 1u).xyz, a_bin), dot(fetch_rows(nr + 2u).xyz, a_bin));\n");
        o.w("  bin = (dot(bin, bin) > 1e-6 && !any(isnan(bin)) && !any(isinf(bin))) ? normalize(bin) : vec3(0.0, 1.0, 0.0);\n");
        o.w("  tan = vec3(dot(fetch_rows(nr).xyz, a_tan), dot(fetch_rows(nr + 1u).xyz, a_tan), dot(fetch_rows(nr + 2u).xyz, a_tan));\n");
        o.w("  tan = (dot(tan, tan) > 1e-6 && !any(isnan(tan)) && !any(isinf(tan))) ? normalize(tan) : vec3(1.0, 0.0, 0.0);\n");
    }

    o.w("  vec4 v4 = vec4(pos, 1.0);\n");
    o.w("  vec4 c = vec4(dot(proj[0], v4), dot(proj[1], v4), dot(proj[2], v4), dot(proj[3], v4));\n");
    o.w("  float depth = (c.z * depthParams.x + c.w * depthParams.y) * (1.0 / 16777215.0);\n");
    o.w("  v_gxz = c.w != 0.0 ? depth / c.w : depth;\n");
    o.w("  bool vrDraw = (fetch_recs(rb + %uu) & 1u) != 0u && vrFlags.x == 1;\n", (unsigned)kRecFlags);
    o.w("  bool stereoDraw = (fetch_recs(rb + %uu) & 1u) != 0u && vrFlags.x == 2;\n", (unsigned)kRecFlags);
    o.w("  vec4 clipPos;\n");
    o.w("  if (vrDraw && (fetch_recs(rb + %uu) & 2u) != 0u) {\n", (unsigned)kRecFlags);
    o.w("    vec4 e = vec4(dot(vrView[0].xyz, pos), dot(vrView[1].xyz, pos), dot(vrView[2].xyz, pos), 0.0);\n");
    o.w("    clipPos = vec4(dot(vrProj[0], e), dot(vrProj[1], e), 0.0, dot(vrProj[3], e));\n");
    o.w("    clipPos.z = clipPos.w * 0.99999;\n");
    if (u.cutaway) o.w("    v_eye = vec3(0.0, 0.0, 1.0e6);\n");
    o.w("  } else if (vrDraw) {\n");
    o.w("    vec4 e = vec4(dot(vrView[0], v4), dot(vrView[1], v4), dot(vrView[2], v4), 1.0);\n");
    o.w("    clipPos = vec4(dot(vrProj[0], e), dot(vrProj[1], e), dot(vrProj[2], e), dot(vrProj[3], e));\n");
    if (u.cutaway) o.w("    v_eye = e.xyz;\n");
    o.w("  } else {\n");
    o.w("    if (stereoDraw) c.x += vrStereo.x * min(((fetch_recs(rb + %uu) & 2u) != 0u ? 0.0 : vrStereo.y) - c.w, vrStereo.z * c.w);\n",
        (unsigned)kRecFlags);
    if (u.hud) {
        o.w("    if (vrFlags.x == 2) {\n");
        o.w("      if ((fetch_recs(rb + %uu) & 4u) != 0u && vrStereo2.y == 0.0) c.x += vrStereo2.x * c.w;\n", (unsigned)kRecFlags);
        o.w("      else c.x = c.x * (1.0 + abs(vrStereo.w)) + vrStereo.w * c.w;\n");
        o.w("    }\n");
    }
    o.w("    clipPos = vec4(c.x, c.y, depth * 2.0 * 1.0 - c.w, c.w);\n");
    o.w("    if ((fetch_recs(rb + %uu) & 2u) != 0u) {\n", (unsigned)kRecFlags);
    o.w("      clipPos.z = clipPos.w * 0.99999;\n");
    o.w("    }\n");
    if (u.cutaway) o.w("    v_eye = vec3(0.0, 0.0, 1.0e6);\n");
    o.w("  }\n");
    o.w("  float z_vk = clipPos.z * 0.5 + clipPos.w * 0.5;\n");
    o.w("  gl_Position = vec4(clipPos.x, -clipPos.y, clipPos.w > 0.0 ? clamp(z_vk, 0.0, clipPos.w) : z_vk, clipPos.w);\n");
    o.w("  v_viewz = pos.z;\n");

    o.w("  vec4 vclr0 = %s;\n", (f & VF_CLR0) ? "a_clr0" : "vec4(1.0)");
    o.w("  vec4 vclr1 = %s;\n", (f & VF_CLR1) ? "a_clr1" : "vec4(1.0)");
    for (int c = 0; c < 2; c++) {
        if ((uint32_t)c >= u.numChans) {
            o.w("  v_clr%d = vclr%d;\n", c, c);
            continue;
        }
        for (int alpha = 0; alpha < 2; alpha++) {
            uint32_t ctrl = alpha ? u.alphaCtrl[c] : u.colorCtrl[c];
            const char* sw = alpha ? "a" : "rgb";
            char mat[64], amb[64];
            snprintf(mat, sizeof(mat), (ctrl & 1) ? "vclr%d.%s" : "matColor[%d].%s", c, sw);
            snprintf(amb, sizeof(amb), ((ctrl >> 6) & 1) ? "vclr%d.%s" : "ambColor[%d].%s", c, sw);
            bool enable = (ctrl >> 1) & 1;
            uint32_t mask = ((ctrl >> 2) & 15) | (((ctrl >> 11) & 15) << 4);
            if (!enable) {
                o.w("  v_clr%d.%s = %s;\n", c, sw, mat);
                continue;
            }
            uint32_t diffuse = (ctrl >> 7) & 3;
            bool atten = (ctrl >> 9) & 1;
            bool spot = (ctrl >> 10) & 1;
            o.w("  {\n    %s lit = %s;\n", alpha ? "float" : "vec3", amb);
            for (int l = 0; l < 8; l++) {
                if (!(mask & (1u << l))) continue;
                o.w("    {\n");
                o.w("      uint b = lbase + %uu;\n", (unsigned)(l * 5));
                o.w("      vec3 lpos = fetch_st(b + 3u).xyz;\n");
                o.w("      vec3 ldir = fetch_st(b + 4u).xyz;\n");
                o.w("      vec3 d = lpos - pos;\n");
                o.w("      float d2 = dot(d, d);\n");
                o.w("      float dist = sqrt(max(d2, 1e-6));\n");
                o.w("      vec3 lvec = d / dist;\n");
                o.w("      float cosAttn = (dist > 0.0 && %s) ? max(0.0, dot(ldir, -lvec)) : 1.0;\n", spot ? "true" : "false");
                o.w("      vec4 k = fetch_st(b + 1u);\n");
                o.w("      float a = (dist > 0.0 && %s) ? max(0.0, dot(vec3(1.0, cosAttn, cosAttn * cosAttn), k.xyz) / dot(vec3(1.0, dist, d2), fetch_st(b + 2u).xyz)) : 1.0;\n",
                    atten ? "true" : "false");
                if (diffuse == 0) {
                    o.w("      float illum = 1.0;\n");
                } else if (diffuse == 1) {
                    o.w("      float illum = max(0.0, dot(nrm, lvec));\n");
                } else {
                    o.w("      float illum = clamp((dot(nrm, lvec) >= 0.0 ? dot(nrm, lvec) : 0.0), 0.0, 1.0);\n");
                }
                o.w("      lit += %s * (fetch_st(b).%s * (illum * a));\n", mat, sw);
                o.w("    }\n");
            }
            o.w("    v_clr%d.%s = clamp(lit, %s(0.0), %s(1.0));\n  }\n", c, sw, alpha ? "float" : "vec3", alpha ? "float" : "vec3");
        }
    }

    for (uint32_t i = 0; i < u.numTexGens && i < 8; i++) {
        uint32_t info = u.texMtxInfo[i];
        uint32_t src = (info >> 7) & 31;
        uint32_t form = (info >> 2) & 3;
        const char* in = "vec4(0.0, 0.0, 1.0, 1.0)";
        char uvBuf[64];
        if (src == 0) in = "vec4(pos, 1.0)";
        else if (src == 1) in = "vec4(nrm, 1.0)";
        else if (src == 2) in = (f & VF_CLR0) ? "vec4(a_clr0.rgb, 1.0)" : "vec4(1.0)";
        else if (src == 3) in = "vec4(bin, 1.0)";
        else if (src == 4) in = "vec4(tan, 1.0)";
        else if (src >= 5 && src <= 12) {
            int texIdx = src - 5;
            if (f & (VF_TEX0 << texIdx)) {
                snprintf(uvBuf, sizeof(uvBuf), "vec4(a_tex%d, 1.0, 1.0)", texIdx);
                in = uvBuf;
            }
        }
        o.w("  {\n    vec4 tsrc = %s;\n", in);
        if (f & VF_TEXMTX) {
            if (i < 4) {
                o.w("    uint m = (a_texmtx.x >> %uu) & 63u;\n", i * 8u);
            } else {
                o.w("    uint m = (a_texmtx.y >> %uu) & 63u;\n", (i - 4u) * 8u);
            }
        } else {
            o.w("    uint m = (fetch_recs(rb + %uu) >> %uu) & 63u;\n", i < 4 ? (unsigned)kRecMtxIdxA : (unsigned)kRecMtxIdxB,
                i < 4 ? (unsigned)((i + 1) * 6) : (unsigned)((i - 4) * 6));
        }
        o.w("    m = (m != 0u) ? m : 60u;\n");
        o.w("    uint r = slotRow(rb, %uu, m);\n", (unsigned)kRecPosSlots);
        if (form == 0) {
            o.w("    vec2 t = vec2(dot(fetch_rows(r), tsrc), dot(fetch_rows(r + 1u), tsrc));\n");
            o.w("    v_tex%u = vec3(t, 1.0);\n", i);
        } else {
            o.w("    v_tex%u = mulRows(r, tsrc);\n", i);
        }
        if (u.dualTex) {
            o.w("    uint pr = fetch_recs(rb + %uu);\n", (unsigned)(kRecPost + i));
            o.w("    v_tex%u = mulRows(pr, vec4(v_tex%u, 1.0));\n", i, i);
        }
        if ((u.screenTexGens >> i) & 1) {
            o.w("    if (vrDraw) {\n");
            o.w("      v_tex%u = vec3((clipPos.xy / clipPos.w) * 0.5 + 0.5, 1.0);\n", i);
            o.w("    } else {\n");
            o.w("      v_tex%u = vec3((c.xy / c.w) * 0.5 + 0.5, 1.0);\n", i);
            o.w("    }\n");
        }
        o.w("  }\n");
    }
    o.w("}\n");
    return o.s;
}

}  // namespace gpu
