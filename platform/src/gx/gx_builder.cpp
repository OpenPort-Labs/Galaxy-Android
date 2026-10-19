#include "gx_builder.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "port/port.h"

namespace gpu {

namespace {

bool isTevColorReg(u32 reg) {
    return reg >= 0xE0 && reg <= 0xE7;
}

}  // namespace

void Builder::build(std::shared_ptr<const Frame> frame, Prepared& out) {
    const Frame& f = *frame;
    items.clear();
    uids.clear();
    uidIndex.clear();
    rows.clear();
    recs.clear();
    states.clear();
    vtxRec.clear();
    indices.clear();
    stateIndex.clear();
    memcpy(bp, f.startBp, sizeof(bp));
    if (bp[0xF6] == 0 && bp[0xF7] == 0) {
        bp[0xF6] = 0x000004;
        bp[0xF7] = 0x00000E;
        bp[0xF8] = 0x000000;
        bp[0xF9] = 0x00000C;
        bp[0xFA] = 0x000005;
        bp[0xFB] = 0x00000D;
        bp[0xFC] = 0x00000A;
        bp[0xFD] = 0x00000E;
    }
    memcpy(tevReg, f.startTevReg, sizeof(tevReg));
    memcpy(tevKonst, f.startTevKonst, sizeof(tevKonst));
    memcpy(xfRegs, f.startXfRegs, sizeof(xfRegs));
    memcpy(xfMem, f.startXfMem, sizeof(xfMem));
    // Hardwired XF identity matrix at rows 60..63 (GX_IDENTITY = 60 = 0x3C):
    xfMem[240] = 1.0f; xfMem[241] = 0.0f; xfMem[242] = 0.0f; xfMem[243] = 0.0f;
    xfMem[244] = 0.0f; xfMem[245] = 1.0f; xfMem[246] = 0.0f; xfMem[247] = 0.0f;
    xfMem[248] = 0.0f; xfMem[249] = 0.0f; xfMem[250] = 1.0f; xfMem[251] = 0.0f;
    xfMem[252] = 0.0f; xfMem[253] = 0.0f; xfMem[254] = 0.0f; xfMem[255] = 1.0f;

    // Post-transform matrix memory rows 60..63 (0x05F0..0x05FF):
    xfMem[1520] = 1.0f; xfMem[1521] = 0.0f; xfMem[1522] = 0.0f; xfMem[1523] = 0.0f;
    // GX_PTIDENTITY (125 - 64 = 61): 3x4 identity matrix starting at row 61 (indices 1524..1535):
    xfMem[1524] = 1.0f; xfMem[1525] = 0.0f; xfMem[1526] = 0.0f; xfMem[1527] = 0.0f;
    xfMem[1528] = 0.0f; xfMem[1529] = 1.0f; xfMem[1530] = 0.0f; xfMem[1531] = 0.0f;
    xfMem[1532] = 0.0f; xfMem[1533] = 0.0f; xfMem[1534] = 1.0f; xfMem[1535] = 0.0f;

    for (u32& v : posSlot) v = ~0u;
    for (u32& v : nrmSlot) v = ~0u;
    for (u32& v : postSlot) v = ~0u;
    lightBase = projBase = pixBase = ~0u;
    inHud = false;
    inSky = false;
    inPlayer = false;
    inPointer = false;
    uidDirty = true;
    lastUidFlags = ~0u;
    drawStateDirty = true;
    cam = CameraInfo();
    for (u32& t : boundTex) t = ~0u;
    clearAR = f.startBp[0x4F];
    clearGB = f.startBp[0x50];
    clearZ = f.startBp[0x51] & 0xFFFFFF;
    u32 ordinal = 0;

    const u32* c = f.cmds.data();
    const u32* end = c + f.cmds.size();
    while (c < end) {
        u32 op = *c++;
        if (op != CMD_DRAW) {
            drawStateDirty = true;
        }
        switch (op) {
        case CMD_XF: {
            u32 addr = c[0], count = c[1];
            c += 2;
            for (u32 i = 0; i < count; i++) {
                u32 a = addr + i;
                if (a < 0x1000) {
                    if (a >= 240 && a <= 255) continue;
                    xfMem[a] = bitsToFloat(c[i]);
                } else if (a < 0x1100) {
                    xfRegs[a - 0x1000] = c[i];
                }
            }
            invalidateXf(addr, addr + count);
            auto touches = [&](u32 lo, u32 hi) { return addr < hi && addr + count > lo; };
            if (touches(0x1009, 0x100A) || touches(0x100E, 0x1013) || touches(0x103F, 0x1048) || touches(0x1050, 0x1058)) {
                uidDirty = true;
            }
            c += count;
            break;
        }
        case CMD_BP: {
            u32 v = *c++;
            u32 reg = v >> 24;
            bp[reg] = v & 0xFFFFFF;
            if (isTevColorReg(reg)) {
                trackTevColorWrite(tevReg, tevKonst, reg, v & 0xFFFFFF);
            }
            if ((reg >= 0x06 && reg <= 0x0E) || (reg >= 0x80 && reg <= 0x8B) || (reg >= 0xA0 && reg <= 0xAB) || (reg >= 0xE0 && reg <= 0xE7) ||
                (reg >= 0xEE && reg <= 0xF4)) {
                pixBase = ~0u;
            }
            if (reg == 0x00 || (reg >= 0x10 && reg <= 0x1F) || (reg >= 0x25 && reg <= 0x2F) || reg == 0x43 || (reg >= 0xC0 && reg <= 0xDF) ||
                reg == 0xF1 || reg == 0xF3 || (reg >= 0xF5 && reg <= 0xFD)) {
                uidDirty = true;
            }
            break;
        }
        case CMD_TEX:
            boundTex[c[0] & 7] = c[1];
            c += 2;
            break;
        case CMD_TEX_EFB:
            boundTex[c[0] & 7] = 0x80000000u | (c[4] >> 5);
            c += 5;
            break;
        case CMD_MARKER: {
            Item it{};
            it.type = IT_MARKER;
            it.marker = *c++;
            if (it.marker == MARK_HUD_BEGIN) inHud = true;
            if (it.marker == MARK_HUD_END) inHud = false;
            if (it.marker == MARK_SKY_BEGIN) inSky = true;
            if (it.marker == MARK_SKY_END) inSky = false;
            if (it.marker == MARK_PLAYER_BEGIN || it.marker == MARK_PLAYER_END) {
                inPlayer = it.marker == MARK_PLAYER_BEGIN;
                drawStateDirty = true;
            }
            if (it.marker == MARK_POINTER_BEGIN || it.marker == MARK_POINTER_END) {
                inPointer = it.marker == MARK_POINTER_BEGIN;
                drawStateDirty = true;
            }
            items.push_back(it);
            break;
        }
        case CMD_CAMERA: {
            if (!cam.valid) {
                cam.valid = true;
                memcpy(cam.view, c, 12 * 4);
                memcpy(cam.pos, c + 12, 3 * 4);
                memcpy(cam.watch, c + 15, 3 * 4);
                memcpy(cam.up, c + 18, 3 * 4);
                memcpy(cam.watchUp, c + 21, 3 * 4);
                memcpy(&cam.fovy, c + 24, 4);
                cam.flags = c[25];
                memcpy(&cam.aspect, c + 26, 4);
                memcpy(cam.player, c + 27, 3 * 4);
                memcpy(cam.playerUp, c + 30, 3 * 4);
            }
            c += kCameraWords;
            break;
        }
        case CMD_EFB_COPY: {
            Item it{};
            it.type = IT_COPY;
            memcpy(it.copy, c, sizeof(it.copy));
            it.copyPixFmt = bp[0x43];
            it.copyCmode = bp[0x41];
            it.copyZmode = bp[0x40];
            c += 10;
            items.push_back(it);
            if ((it.copy[9] >> 14) & 1) {
                u32 w = (it.copy[2] & 0x3FF) + 1;
                u32 h = ((it.copy[2] >> 10) & 0x3FF) + 1;
                if (w >= 600 && h >= 400) {
                    nativeW = w;
                    nativeH = h;
                }
            }
            break;
        }
        case CMD_DRAW: {
            u32 prim = c[0], flags = c[1], first = c[2], count = c[3];
            c += 4;
            emitDraw(prim, flags, first, count, ordinal);
            break;
        }
        default:
            port_log("builder: bad recording opcode %u", op);
            c = end;
            break;
        }
    }

    if (rows.empty()) rows.resize(4, 0.0f);
    if (recs.empty()) recs.resize(kRecWords, 0);
    if (states.empty()) states.resize(4, 0.0f);
    if (vtxRec.empty()) vtxRec.push_back(0);

    out.frame = std::move(frame);
    out.number = f.number;
    std::swap(out.items, items);
    std::swap(out.uids, uids);
    std::swap(out.rows, rows);
    std::swap(out.recs, recs);
    std::swap(out.states, states);
    std::swap(out.vtxRec, vtxRec);
    std::swap(out.indices, indices);
    out.cam = cam;
    out.clearAR = clearAR;
    out.clearGB = clearGB;
    out.clearZ = clearZ;
    out.nativeW = nativeW;
    out.nativeH = nativeH;
}

void logItems(const Prepared& p) {
    const std::vector<Item>& items = p.items;
    const std::vector<float>& rows = p.rows;
    const std::vector<u32>& recs = p.recs;
    const std::vector<float>& states = p.states;
    const std::vector<u32>& indices = p.indices;
    u64 preparedNumber = p.number;
    u32 nativeW = p.nativeW, nativeH = p.nativeH;
    port_log("items: frame %llu, %zu items, native %ux%u, %zu rows, %zu records, %zu state vec4s, %zu indices", (unsigned long long)preparedNumber,
             items.size(), nativeW, nativeH, rows.size() / 4, recs.size() / kRecWords, states.size() / 4, indices.size());
    for (const Item& it : items) {
        if (it.type == IT_MARKER) {
            port_log("items: marker %u", it.marker);
        } else if (it.type == IT_COPY) {
            const u32* cp = it.copy;
            u32 ctrl = cp[9];
            port_log("items: COPY src %u,%u %ux%u dst %06x %s fmt %u clear %u (color %06x %06x z %06x) cmode %06x zmode %06x", cp[1] & 0x3FF,
                     (cp[1] >> 10) & 0x3FF, (cp[2] & 0x3FF) + 1, ((cp[2] >> 10) & 0x3FF) + 1, cp[3], (ctrl >> 14) & 1 ? "XFB" : "TEX",
                     ((ctrl >> 4) & 7) | ((ctrl >> 0) & 8), (ctrl >> 11) & 1, cp[6], cp[7], cp[8], it.copyCmode, it.copyZmode);
        } else {
            port_log("items: batch %u draws prog %u prim %x flags %05x idx %u vr %u hud %u player %u blend %u(%x,%x,%x) cmask %x z %u/%u/%x cull %u vp %.0f,%.0f "
                     "%.0fx%.0f tex0 %08x tex1 %08x screen texgens %x",
                     it.draws, it.program, it.prim, it.flags, it.idxCount, it.useVr, it.hud, it.player, it.blend, it.srcRGB, it.dstRGB, it.eq, it.colorMask,
                     it.depthTest, it.depthWrite, it.depthFunc, it.cull, it.vp[0], it.vp[1], it.vp[2], it.vp[3], it.tex[0], it.tex[1],
                     p.uids[it.uid].screenTexGens);
            if (it.player) {
                const ShaderUid& u = p.uids[it.uid];
                port_log("items:   player: chans %u cc %06x %06x ac %06x %06x; texgens %u: %05x %05x %05x %05x post %03x %03x; tev %u ind %u; "
                         "tex %08x %08x %08x %08x",
                         u.numChans, u.colorCtrl[0], u.colorCtrl[1], u.alphaCtrl[0], u.alphaCtrl[1], u.numTexGens, u.texMtxInfo[0],
                         u.texMtxInfo[1], u.texMtxInfo[2], u.texMtxInfo[3], u.postMtxInfo[0], u.postMtxInfo[1], u.numTevStages, u.numIndStages,
                         it.tex[0], it.tex[1], it.tex[2], it.tex[3]);
                port_log("items:   player tev: order %06x %06x; color %06x %06x %06x %06x; alpha %06x %06x %06x %06x; ksel %06x %06x %06x %06x",
                         u.tevOrder[0], u.tevOrder[1], u.tevColor[0], u.tevColor[1], u.tevColor[2], u.tevColor[3], u.tevAlpha[0], u.tevAlpha[1],
                         u.tevAlpha[2], u.tevAlpha[3], u.ksel[0], u.ksel[1], u.ksel[2], u.ksel[3]);
            }
        }
    }
}

}  // namespace gpu
