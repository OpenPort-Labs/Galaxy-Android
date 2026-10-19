#include "gx_vertex_loader.h"
#include "gx_vertex_reader.h"

#include <cmath>
#include <cstring>

namespace gpu {

static const u8 kCompSize[8] = {1, 1, 2, 2, 4, 0, 0, 0};

struct AttrDesc {
    u32 type;
    u32 cnt, fmt, shift;
};

template <bool Track>
static u32 loadVerticesT(WordBuffer& out, const u8* data, u32 count, int vat, u32 stride, ArrayUse* use) {
    const CPState& cp = g.cp;
    u32 lo = cp.vcdLo, hi = cp.vcdHi;
    u32 a = cp.vatA[vat], b = cp.vatB[vat], c = cp.vatC[vat];

    bool pnmtx = lo & 1;
    bool texmtx = ((lo >> 1) & 0xFF) != 0;
    AttrDesc pos = {(lo >> 9) & 3, (a >> 0) & 1, (a >> 1) & 7, (a >> 4) & 31};
    AttrDesc nrm = {(lo >> 11) & 3, (a >> 9) & 1, (a >> 10) & 7, 0};
    bool nrmIndex3 = (a >> 31) & 1;
    AttrDesc clr[2] = {{(lo >> 13) & 3, (a >> 13) & 1, (a >> 14) & 7, 0}, {(lo >> 15) & 3, (a >> 17) & 1, (a >> 18) & 7, 0}};
    AttrDesc tex[8];
    tex[0] = {(hi >> 0) & 3, (a >> 21) & 1, (a >> 22) & 7, (a >> 25) & 31};
    tex[1] = {(hi >> 2) & 3, (b >> 0) & 1, (b >> 1) & 7, (b >> 4) & 31};
    tex[2] = {(hi >> 4) & 3, (b >> 9) & 1, (b >> 10) & 7, (b >> 13) & 31};
    tex[3] = {(hi >> 6) & 3, (b >> 18) & 1, (b >> 19) & 7, (b >> 22) & 31};
    tex[4] = {(hi >> 8) & 3, (b >> 27) & 1, (b >> 28) & 7, (c >> 0) & 31};
    tex[5] = {(hi >> 10) & 3, (c >> 5) & 1, (c >> 6) & 7, (c >> 9) & 31};
    tex[6] = {(hi >> 12) & 3, (c >> 14) & 1, (c >> 15) & 7, (c >> 18) & 31};
    tex[7] = {(hi >> 14) & 3, (c >> 23) & 1, (c >> 24) & 7, (c >> 27) & 31};

    u32 flags = 0;
    if (pnmtx) flags |= VF_POSMTX;
    if (nrm.type) flags |= nrm.cnt ? (VF_NRM | VF_NBT) : VF_NRM;
    if (clr[0].type) flags |= VF_CLR0;
    if (clr[1].type) flags |= VF_CLR1;
    for (int i = 0; i < 8; i++) {
        if (tex[i].type) flags |= VF_TEX0 << i;
    }
    if (texmtx) flags |= VF_TEXMTX;

    float posScale = (pos.fmt == 4) ? 1.0f : ldexpf(1.0f, -(int)pos.shift);
    float nrmScale = (nrm.fmt == 1) ? 1.0f / 64.0f : (nrm.fmt == 3) ? 1.0f / 16384.0f : 1.0f;
    int texList[8], texCount = 0;
    float texScale[8];
    for (int i = 0; i < 8; i++) {
        if (tex[i].type) {
            texList[texCount++] = i;
            texScale[i] = (tex[i].fmt == 4) ? 1.0f : ldexpf(1.0f, -(int)tex[i].shift);
        }
    }

    const u8* arrayHost[12] = {};
    auto resolveArray = [&](int array, u32 type) {
        if (type >= ATTR_INDEX8) {
            arrayHost[array] = memPtr(cp.arrayBase[array]);
        }
    };
    resolveArray(0, pos.type);
    resolveArray(1, nrm.type);
    resolveArray(2, clr[0].type);
    resolveArray(3, clr[1].type);
    for (int t = 0; t < 8; t++) {
        resolveArray(4 + t, tex[t].type);
    }
    if (Track) {
        static const u8 kClrBytes[8] = {2, 3, 4, 2, 3, 4, 4, 4};
        u32 elem[12];
        elem[0] = (pos.cnt ? 3 : 2) * kCompSize[pos.fmt];
        elem[1] = (nrm.cnt && !nrmIndex3 ? 9 : 3) * kCompSize[nrm.fmt];
        elem[2] = kClrBytes[clr[0].fmt];
        elem[3] = kClrBytes[clr[1].fmt];
        for (int t = 0; t < 8; t++) {
            elem[4 + t] = (tex[t].cnt ? 2 : 1) * kCompSize[tex[t].fmt];
        }
        for (int i = 0; i < 12; i++) {
            if (arrayHost[i]) {
                if (use->lo[i] <= use->hi[i] && (use->base[i] != cp.arrayBase[i] || use->stride[i] != cp.arrayStride[i])) {
                    use->lo[i] = 0;
                    use->hi[i] = ~0u;
                }
                use->base[i] = cp.arrayBase[i];
                use->stride[i] = cp.arrayStride[i];
                use->elemBytes[i] = elem[i];
            }
        }
    }
    auto arrayPtr = [&](int array, u32 index) -> const u8* {
        if (Track) {
            use->lo[array] = index < use->lo[array] ? index : use->lo[array];
            use->hi[array] = index > use->hi[array] ? index : use->hi[array];
        }
        return arrayHost[array] + index * cp.arrayStride[array];
    };
    auto readIndex = [](const u8*& v, u32 type) -> u32 {
        if (type == ATTR_INDEX8) {
            return *v++;
        }
        u32 idx = ((u32)v[0] << 8) | v[1];
        v += 2;
        return idx;
    };
    u32 texmtxMask = (lo >> 1) & 0xFF;
    u32 defaultTexMtx[8];
    for (int i = 0; i < 8; i++) {
        u32 m = i < 4 ? (cp.matIndexA >> (6 * (i + 1))) & 63 : (cp.matIndexB >> (6 * (i - 4))) & 63;
        defaultTexMtx[i] = (m != 0) ? m : 60;
    }

    size_t base = out.size();
    out.resize(base + (size_t)count * vtxWords(flags));
    u32* w = out.data() + base;

    for (u32 n = 0; n < count; n++) {
        const u8* v = data + (size_t)n * stride;
        u32 midx[9] = {0};
        if (pnmtx) {
            midx[0] = *v++ & 63;
        }
        if (texmtxMask) {
            for (int i = 0; i < 8; i++) {
                midx[1 + i] = (texmtxMask >> i) & 1 ? (*v++ & 63) : defaultTexMtx[i];
            }
        }

        // Position
        {
            u32 comps = pos.cnt ? 3 : 2;
            if (pos.type < ATTR_INDEX8) {
                v = readComps(v, pos.fmt, true, comps, posScale, w);
            } else {
                readComps(arrayPtr(0, readIndex(v, pos.type)), pos.fmt, false, comps, posScale, w);
            }
            if (!pos.cnt) {
                *w++ = 0;
            }
        }
        if (pnmtx) {
            *w++ = midx[0];
        }

        // Normal
        if (nrm.type) {
            u32 vecs = nrm.cnt ? 3 : 1;
            if (nrm.type == ATTR_DIRECT) {
                v = readComps(v, nrm.fmt, true, vecs * 3, nrmScale, w);
            } else if (nrm.cnt && nrmIndex3) {
                u32 idx[3];
                for (int k = 0; k < 3; k++) {
                    idx[k] = readIndex(v, nrm.type);
                }
                for (int k = 0; k < 3; k++) {
                    readComps(arrayPtr(1, idx[k]), nrm.fmt, false, 3, nrmScale, w);
                }
            } else {
                readComps(arrayPtr(1, readIndex(v, nrm.type)), nrm.fmt, false, vecs * 3, nrmScale, w);
            }
        }

        // Colors
        for (int ci = 0; ci < 2; ci++) {
            if (!clr[ci].type) {
                continue;
            }
            const u8* src = clr[ci].type == ATTR_DIRECT ? v : arrayPtr(2 + ci, readIndex(v, clr[ci].type));
            u32 color;
            if (clr[ci].fmt == 5) {
                memcpy(&color, src, 4);
                src += 4;
            } else {
                VertexReader r{src, clr[ci].type == ATTR_DIRECT};
                color = r.color(clr[ci].fmt);
                src = r.p;
            }
            *w++ = color;
            if (clr[ci].type == ATTR_DIRECT) {
                v = src;
            }
        }

        // Texture coordinates
        for (int ti = 0; ti < texCount; ti++) {
            int t = texList[ti];
            u32 comps = tex[t].cnt ? 2 : 1;
            if (tex[t].type == ATTR_DIRECT) {
                v = readComps(v, tex[t].fmt, true, comps, texScale[t], w);
            } else {
                readComps(arrayPtr(4 + t, readIndex(v, tex[t].type)), tex[t].fmt, false, comps, texScale[t], w);
            }
            if (!tex[t].cnt) {
                *w++ = 0;
            }
        }

        if (texmtx) {
            *w++ = midx[1] | (midx[2] << 8) | (midx[3] << 16) | (midx[4] << 24);
            *w++ = midx[5] | (midx[6] << 8) | (midx[7] << 16) | (midx[8] << 24);
        }
    }
    return flags;
}

u32 loadVertices(WordBuffer& out, const u8* data, u32 count, int vat, u32 stride) {
    return loadVerticesT<false>(out, data, count, vat, stride, nullptr);
}

u32 loadVerticesTracked(WordBuffer& out, const u8* data, u32 count, int vat, u32 stride, ArrayUse* use) {
    return loadVerticesT<true>(out, data, count, vat, stride, use);
}

}  // namespace gpu
