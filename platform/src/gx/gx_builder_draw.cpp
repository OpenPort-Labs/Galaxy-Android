#include "gx_builder.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace gpu {

static u32 usedTexUnits(const u32* bp) {
    u32 gen = bp[0x00];
    u32 stages = ((gen >> 10) & 15) + 1, ind = (gen >> 16) & 7;
    u32 mask = 0;
    for (u32 st = 0; st < stages; st++) {
        u32 order = (bp[0x28 + st / 2] >> ((st & 1) * 12)) & 0xFFF;
        if ((order >> 6) & 1) mask |= 1u << (order & 7);
    }
    for (u32 st = 0; st < ind; st++) mask |= 1u << ((bp[0x27] >> (st * 6)) & 7);
    return mask;
}

u32 Builder::screenTexGens(const Frame& f, const Item& it, u32 first, const ShaderUid& uid) {
    return 0;
}

u32 Builder::uidFor(const ShaderUid& uid) {
    auto it = uidIndex.find(uid);
    if (it != uidIndex.end()) return it->second;
    u32 i = (u32)uids.size();
    uids.push_back(uid);
    uidIndex.emplace(uid, i);
    return i;
}

void Builder::appendIndices(u32 kind, u32 base, u32 count) {
    u32 n;
    switch (kind) {
    case 0x80:
    case 0x88:
        n = count / 4 * 6;
        break;
    case 0x90:
        n = count / 3 * 3;
        break;
    case 0x98:
    case 0xA0:
        n = count >= 3 ? (count - 2) * 3 : 0;
        break;
    case 0xA8:
        n = count / 2 * 2;
        break;
    case 0xB0:
        n = count >= 2 ? (count - 1) * 2 : 0;
        break;
    default:
        n = count;
        break;
    }
    size_t at = indices.size();
    indices.resize(at + n);
    u32* o = indices.data() + at;
    switch (kind) {
    case 0x80:
    case 0x88:
        for (u32 v = base; v + 4 <= base + count; v += 4, o += 6) {
            o[0] = v, o[1] = v + 1, o[2] = v + 2, o[3] = v, o[4] = v + 2, o[5] = v + 3;
        }
        break;
    case 0x98:
        for (u32 i = 0; i + 2 < count; i++, o += 3) {
            o[0] = base + i + (i & 1), o[1] = base + i + 1 - (i & 1), o[2] = base + i + 2;
        }
        break;
    case 0xA0:
        for (u32 i = 1; i + 1 < count; i++, o += 3) {
            o[0] = base, o[1] = base + i, o[2] = base + i + 1;
        }
        break;
    case 0xB0:
        for (u32 i = 0; i + 1 < count; i++, o += 2) {
            o[0] = base + i, o[1] = base + i + 1;
        }
        break;
    default:
        for (u32 i = 0; i < n; i++) {
            o[i] = base + i;
        }
        break;
    }
}

void Builder::emitDraw(u32 prim, u32 flags, u32 first, u32 count, u32& ordinal) {
    if (!count) return;
    u32 kind = prim & 0xF8;
    uint32_t glPrim = kind <= 0xA0 ? 4 : kind <= 0xB0 ? 1 : 0;
    if (!drawStateDirty && lastDrawReusable && flags == lastDrawFlags && glPrim == lastDrawPrim && !items.empty() &&
        items.back().type == IT_DRAW) {
        Item* b = &items.back();
        if (b->endWord != first) {
            Item it = *b;
            it.firstWord = first;
            it.firstOrdinal = ordinal;
            it.idxOffset = (u32)indices.size();
            it.idxCount = 0;
            it.draws = 0;
            items.push_back(it);
            b = &items.back();
        }
        vtxRec.insert(vtxRec.end(), count, lastDrawRec);
        appendIndices(kind, ordinal - b->firstOrdinal, count);
        ordinal += count;
        b->idxCount = (u32)indices.size() - b->idxOffset;
        b->endWord = first + count * vtxWords(flags);
        b->draws++;
        return;
    }
    Item it{};
    it.type = IT_DRAW;
    it.flags = flags;
    it.hud = inHud;
    it.player = inPlayer;
    it.prim = glPrim;
    float sx = bitsToFloat(xfRegs[0x1A]), sy = bitsToFloat(xfRegs[0x1B]);
    float ox = bitsToFloat(xfRegs[0x1D]), oy = bitsToFloat(xfRegs[0x1E]);
    float xoff = (float)((bp[0x59] & 0x3FF) * 2), yoff = (float)(((bp[0x59] >> 10) & 0x3FF) * 2);
    it.vp[0] = ox - fabsf(sx) - xoff;
    it.vp[1] = oy - fabsf(sy) - yoff;
    it.vp[2] = 2.0f * fabsf(sx);
    it.vp[3] = 2.0f * fabsf(sy);
    int soffX = (int)xoff, soffY = (int)yoff;
    it.sc[0] = (int)((bp[0x20] >> 12) & 0x7FF) - soffX;
    it.sc[1] = (int)(bp[0x20] & 0x7FF) - soffY;
    it.sc[2] = (int)((bp[0x21] >> 12) & 0x7FF) - soffX + 1;
    it.sc[3] = (int)(bp[0x21] & 0x7FF) - soffY + 1;

    bool perspective = !(xfRegs[0x26] & 1);
    bool fullscreen = it.vp[0] <= 1.0f && it.vp[1] <= 1.0f && it.vp[2] >= (float)nativeW - 2.0f && it.vp[3] >= (float)nativeH - 2.0f;
    it.useVr = perspective && fullscreen && !inHud && cam.valid;
    float p1 = bitsToFloat(xfRegs[0x21]);
    it.hudSafe = inHud && !perspective && (fabsf(p1) < 0.5f);

    if (uidDirty || flags != lastUidFlags || inHud != lastUidHud) {
        ShaderUid uid = makeShaderUid(bp, xfRegs, flags, inHud);
        uid.screenTexGens = 0;
        it.uid = uidFor(uid);
        uidDirty = false;
        lastUid = it.uid;
        lastUidFlags = flags;
        lastUidHud = inHud;
    } else {
        it.uid = lastUid;
    }
    const ShaderUid& uid = uids[it.uid];

    u32 cm = bp[0x41];
    it.colorMask = (((cm >> 3) & 1) ? 7 : 0) | (((cm >> 4) & 1) ? 8 : 0);
    if ((bp[0x43] & 7) != 1) {
        it.colorMask &= 7;
    }
    it.blend = cm & 1;
    static const uint32_t srcF[8] = {0, 1, 0x0306, 0x0307, 0x0302, 0x0303, 0x0304, 0x0305};
    static const uint32_t dstF[8] = {0, 1, 0x0300, 0x0301, 0x0302, 0x0303, 0x0304, 0x0305};
    if ((cm >> 11) & 1) {
        it.blend = 1;
        it.srcRGB = 1;
        it.dstRGB = 1;
        it.eq = 0x8008;
    } else {
        it.srcRGB = srcF[(cm >> 8) & 7];
        it.dstRGB = dstF[(cm >> 5) & 7];
        it.eq = 0x8006;
    }
    it.dstAlpha = (bp[0x42] >> 8) & 1;
    it.dstAlphaValue = (bp[0x42] & 0xFF) / 255.0f;
    u32 zm = bp[0x40];
    it.depthTest = zm & 1;
    it.depthWrite = (zm >> 4) & 1;
    static const uint32_t zf[8] = {0x0200, 0x0201, 0x0202, 0x0203, 0x0204, 0x0205, 0x0206, 0x0207};
    it.depthFunc = zf[(zm >> 1) & 7];
    u32 cull = (bp[0x00] >> 14) & 3;
    it.cull = (cull != 0);
    it.cullFace = (cull == 1) ? 0x0404 : (cull == 2) ? 0x0405 : 0x0408;

    it.texMask = usedTexUnits(bp);
    static const u8 m0[8] = {0x80, 0x81, 0x82, 0x83, 0xA0, 0xA1, 0xA2, 0xA3};
    for (int t = 0; t < 8; t++) {
        bool used = (it.texMask >> t) & 1;
        it.tex[t] = used ? boundTex[t] : ~0u;
        it.texMode0[t] = used ? bp[m0[t]] : 0;
        it.texMode1[t] = used ? bp[m0[t] + 4] : 0;
    }

    u32 rec = drawRecord(it.useVr, inSky, inPointer, uid.dualTex != 0, uid.numTexGens);
    u32 firstOrdinal = ordinal;
    ordinal += count;
    vtxRec.insert(vtxRec.end(), count, rec);
    drawStateDirty = false;
    lastDrawReusable = !uidDirty;
    lastDrawFlags = flags;
    lastDrawPrim = it.prim;
    lastDrawRec = rec;

    Item* b = (!items.empty() && items.back().type == IT_DRAW) ? &items.back() : nullptr;
    if (!b || b->endWord != first || !sameBatchState(*b, it)) {
        it.firstWord = first;
        it.firstOrdinal = firstOrdinal;
        it.idxOffset = (u32)indices.size();
        items.push_back(it);
        b = &items.back();
    }
    appendIndices(kind, firstOrdinal - b->firstOrdinal, count);
    b->idxCount = (u32)indices.size() - b->idxOffset;
    b->endWord = first + count * vtxWords(flags);
    b->draws++;
}

}  // namespace gpu
