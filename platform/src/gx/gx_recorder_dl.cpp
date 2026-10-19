#include "gx_recorder_internal.h"

namespace gpu {

u64 hashRange(const u8* p, size_t n) {
    const u64 k1 = 0x9E3779B185EBCA87ull, k2 = 0xC2B2AE3D27D4EB4Full;
    u64 h[4] = {k1 + k2, k2, 0, 0 - k1};
    auto round = [&](u64 acc, u64 v) {
        acc += v * k2;
        acc = (acc << 31) | (acc >> 33);
        return acc * k1;
    };
    size_t i = 0;
    for (; i + 32 <= n; i += 32) {
        u64 v[4];
        memcpy(v, p + i, 32);
        h[0] = round(h[0], v[0]);
        h[1] = round(h[1], v[1]);
        h[2] = round(h[2], v[2]);
        h[3] = round(h[3], v[3]);
    }
    u64 r = h[0] ^ (h[1] * 3) ^ (h[2] * 5) ^ (h[3] * 7) ^ n;
    for (; i + 8 <= n; i += 8) {
        u64 v;
        memcpy(&v, p + i, 8);
        r = round(r, v);
    }
    for (; i < n; i++) {
        r = round(r, p[i]);
    }
    r ^= r >> 29;
    r *= k1;
    return r ^ (r >> 32);
}

void VtxState::capture() {
    const CPState& cp = g.cp;
    vcdLo = cp.vcdLo;
    vcdHi = cp.vcdHi;
    matIndexA = cp.matIndexA;
    matIndexB = cp.matIndexB;
    for (int i = 0; i < 8; i++) {
        vat[i][0] = cp.vatA[i];
        vat[i][1] = cp.vatB[i];
        vat[i][2] = cp.vatC[i];
    }
    memcpy(arrayBase, cp.arrayBase, sizeof(arrayBase));
    memcpy(arrayStride, cp.arrayStride, sizeof(arrayStride));
}

u64 Recorder::arrayHash(u32 addr, u32 bytes) {
    if (flushHashesStream != gStreamSerial) {
        flushHashes.clear();
        flushHashesStream = gStreamSerial;
    }
    u64 key = ((u64)addr << 32) | bytes;
    auto it = flushHashes.find(key);
    if (it != flushHashes.end()) {
        return it->second;
    }
    dlStats.hashedBytes += bytes;
    u64 h = hashRange(memPtr(addr), bytes);
    flushHashes.emplace(key, h);
    return h;
}

bool Recorder::depsCurrent(const DlEntry& e) {
    for (const ArrayDep& d : e.deps) {
        if (arrayHash(d.addr, d.bytes) != d.hash) {
            return false;
        }
    }
    return true;
}

bool Recorder::displayListBegin(u32 addr, const u8* data, u32 size) {
    dlSlot = nullptr;
    dlEntry = nullptr;
    if (!dlCacheOn) {
        return false;
    }
    dlStats.calls++;
    DlSlot& slot = dlCache[((u64)addr << 32) | size];
    dlSlot = &slot;
    if (frameNumber < slot.noDrawsUntil) {
        return false;
    }
    VtxState st;
    st.capture();
    DlEntry* victim = nullptr;
    for (DlEntry& e : slot.ways) {
        if (e.valid && e.state == st && e.bytes.size() == size && memcmp(e.bytes.data(), data, size) == 0 && depsCurrent(e)) {
            e.lastUsed = frameNumber;
            dlStats.hits++;
            if (e.drawsOnly && !dlCheck) {
                replayDraws(e);
                dlSlot = nullptr;
                return true;
            }
            dlEntry = &e;
            dlReplay = true;
            dlDrawIndex = 0;
            return false;
        }
        if (!victim || !e.valid || (victim->valid && e.lastUsed < victim->lastUsed)) {
            victim = &e;
        }
    }
    if ((!victim || victim->valid) && slot.ways.size() < 4) {
        slot.ways.emplace_back();
        victim = &slot.ways.back();
    }
    DlEntry& e = *victim;
    e.valid = false;
    e.bytes.assign(data, data + size);
    e.state = st;
    e.words.clear();
    e.draws.clear();
    e.deps.clear();
    e.lastUsed = frameNumber;
    e.drawsOnly = true;
    dlEntry = &e;
    dlReplay = false;
    dlUse.reset();
    dlStats.recorded++;
    return false;
}

void Recorder::replayDraws(const DlEntry& e) {
    resolveTextures();
    WordBuffer& verts = frame->verts;
    size_t base = verts.size();
    verts.resize(base + e.words.size());
    memcpy(verts.data() + base, e.words.data(), e.words.size() * 4);
    u32* cmd = appendCmds(5 * e.draws.size());
    for (const DlDraw& d : e.draws) {
        cmd[0] = CMD_DRAW;
        cmd[1] = d.prim;
        cmd[2] = d.flags;
        cmd[3] = (u32)base + d.word;
        cmd[4] = d.count;
        cmd += 5;
    }
    frame->draws += (u32)e.draws.size();
    dlStats.copiedWords += e.words.size();
}

void Recorder::noteListLoad() {
    if (dlEntry && !dlReplay) {
        dlEntry->drawsOnly = false;
    }
}

void Recorder::displayListEnd() {
    if (dlEntry && !dlReplay) {
        DlEntry& e = *dlEntry;
        if (e.draws.empty()) {
            dlSlot->noDrawsUntil = frameNumber + 300;
            e.bytes.clear();
            e.bytes.shrink_to_fit();
        } else {
            bool ok = true;
            for (int i = 0; i < 12; i++) {
                if (dlUse.lo[i] > dlUse.hi[i]) {
                    continue;
                }
                if (dlUse.hi[i] == ~0u) {
                    ok = false;
                    break;
                }
                u32 addr = dlUse.base[i] + dlUse.lo[i] * dlUse.stride[i];
                u32 bytes = (dlUse.hi[i] - dlUse.lo[i]) * dlUse.stride[i] + dlUse.elemBytes[i];
                e.deps.push_back({addr, bytes, arrayHash(addr, bytes)});
            }
            e.valid = ok;
        }
    }
    dlSlot = nullptr;
    dlEntry = nullptr;
}

u32 Recorder::drawVertices(int prim, int vat, u32 count, const u8* data, u32 vertexStride) {
    WordBuffer& verts = frame->verts;
    size_t first = verts.size();
    if (dlEntry && dlReplay) {
        if (dlDrawIndex < dlEntry->draws.size() && dlEntry->draws[dlDrawIndex].count == count) {
            const DlDraw& d = dlEntry->draws[dlDrawIndex++];
            size_t n = (size_t)count * vtxWords(d.flags);
            verts.resize(first + n);
            memcpy(verts.data() + first, dlEntry->words.data() + d.word, n * 4);
            dlStats.copiedWords += n;
            if (dlCheck) {
                dlCheckScratch.clear();
                u32 flags = loadVertices(dlCheckScratch, data, count, vat, vertexStride);
                dlCheckDraws++;
                if (flags != d.flags || (u32)prim != d.prim || dlCheckScratch.size() != n ||
                    memcmp(dlCheckScratch.data(), verts.data() + first, n * 4) != 0) {
                    if (dlCheckBad++ < 20) {
                        port_log("dlcheck: cached draw differs: prim %x/%x flags %x/%x words %zu/%zu", prim, d.prim, flags, d.flags,
                                 dlCheckScratch.size(), n);
                    }
                }
                if (dlCheckDraws % 200000 == 0) {
                    port_log("dlcheck: %llu cached draws compared, %llu differed", (unsigned long long)dlCheckDraws, (unsigned long long)dlCheckBad);
                }
            }
            return d.flags;
        }
        dlEntry->valid = false;
        dlEntry = nullptr;
    } else if (dlEntry) {
        u32 flags = loadVerticesTracked(verts, data, count, vat, vertexStride, &dlUse);
        DlDraw d{(u32)prim, (u32)dlEntry->words.size(), count, flags};
        dlEntry->words.insert(dlEntry->words.end(), verts.begin() + first, verts.end());
        dlEntry->draws.push_back(d);
        dlStats.decodedWords += verts.size() - first;
        return flags;
    } else if (dlSlot && dlSlot->noDrawsUntil) {
        dlSlot->noDrawsUntil = 0;
    }
    u32 flags = loadVertices(verts, data, count, vat, vertexStride);
    dlStats.decodedWords += verts.size() - first;
    return flags;
}

}  // namespace gpu
