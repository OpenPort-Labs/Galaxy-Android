#include "gx_recorder_internal.h"

namespace gpu {

static inline u32 toBits(float f) {
    u32 v;
    memcpy(&v, &f, 4);
    return v;
}

Recorder* sRecorder = nullptr;

Recorder::Recorder() {
    memset(tlutMem, 0, sizeof(tlutMem));
    resetBindings();
    snapshot();
}

void Recorder::snapshot() {
    memcpy(frame->startBp, g.bp, sizeof(frame->startBp));
    memcpy(frame->startTevReg, g.tevReg, sizeof(frame->startTevReg));
    memcpy(frame->startTevKonst, g.tevKonst, sizeof(frame->startTevKonst));
    memcpy(frame->startXfRegs, g.xf.regs, sizeof(frame->startXfRegs));
    memcpy(frame->startXfMem, g.xf.mem, sizeof(frame->startXfMem));
}

void Recorder::resetBindings() {
    for (u32& b : boundTex) {
        b = ~0u;
    }
    texDirty = true;
    bindEpoch++;
    frameTexIndex.clear();
}

u32* Recorder::appendCmds(size_t n) {
    WordBuffer& c = frame->cmds;
    size_t at = c.size();
    c.resize(at + n);
    return c.data() + at;
}

void Recorder::draw(int primitive, int vat, u32 count, const u8* data, u32 vertexStride) {
    resolveTextures();
    u32 first = (u32)frame->verts.size();
    u32 flags = drawVertices(primitive, vat, count, data, vertexStride);
    u32* cmd = appendCmds(5);
    cmd[0] = CMD_DRAW;
    cmd[1] = (u32)primitive;
    cmd[2] = flags;
    cmd[3] = first;
    cmd[4] = count;
    frame->draws++;
}

void Recorder::bpWrite(u32 reg, u32 value, u32) {
    noteListLoad();
    u32* cmd = appendCmds(2);
    cmd[0] = CMD_BP;
    cmd[1] = (reg << 24) | (value & 0xFFFFFF);
    if ((reg >= 0x80 && reg <= 0xBB) || (reg >= 0x28 && reg <= 0x2F) || reg == 0x00 || reg == 0x27 || reg == 0x66) {
        texDirty = true;
    }
}

void Recorder::xfWrite(u32 addr, u32 count) {
    noteListLoad();
    auto& c = cmds();
    size_t at = c.size();
    c.resize(at + 3 + count);
    u32* w = c.data() + at;
    w[0] = CMD_XF;
    w[1] = addr;
    w[2] = count;
    for (u32 i = 0; i < count; i++) {
        u32 a = addr + i;
        u32 v = 0;
        if (a < 0x1000) {
            v = toBits(g.xf.mem[a]);
        } else if (a < 0x1100) {
            v = g.xf.regs[a - 0x1000];
        }
        w[3 + i] = v;
    }
}

void Recorder::cpWrite(u32 reg, u32) {
    noteListLoad();
    if ((reg & 0xF0) == 0x30) {
        xfWrite(0x1018, 1);
    } else if ((reg & 0xF0) == 0x40) {
        xfWrite(0x1019, 1);
    }
}

void Recorder::efbCopy(bool toXfb, bool clear) {
    noteListLoad();
    (void)clear;
    const u32* bp = g.bp;
    u32 id = nextCopyId++;
    auto& c = cmds();
    c.push_back(CMD_EFB_COPY);
    c.push_back(id);
    static const u8 regs[9] = {0x49, 0x4A, 0x4B, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52};
    for (u8 r : regs) {
        c.push_back(bp[r]);
    }
    if (toXfb) {
        frame->xfbCopies++;
        return;
    }
    u32 addr = (bp[0x4B] & 0xFFFFFF) << 5;
    u32 w = (bp[0x4A] & 0x3FF) + 1, h = ((bp[0x4A] >> 10) & 0x3FF) + 1;
    u32 ctrl = bp[0x52];
    if ((ctrl >> 9) & 1) {
        w = (w + 1) / 2;
        h = (h + 1) / 2;
    }
    u32 fmt = ((ctrl >> 4) & 7) | (((ctrl >> 3) & 1) << 3);
    EfbCopyInfo info{id, w, h, fmt, frameNumber, 0};
    info.memHash = hashBytes(memPtr(addr), 64);
    efbCopies[addr] = info;
    texDirty = true;
    bindEpoch++;
}

void Recorder::loadTlut(u32 regValue) {
    noteListLoad();
    u32 src = (g.bp[0x64] & 0xFFFFFF) << 5;
    u32 tmem = (regValue & 0x3FF) << 9;
    u32 lines = (regValue >> 10) & 0x7FF;
    u32 bytes = lines * 32;
    if (tmem + bytes > sizeof(tlutMem)) {
        bytes = sizeof(tlutMem) - (tmem & (sizeof(tlutMem) - 1));
    }
    memcpy(tlutMem + (tmem & (sizeof(tlutMem) - 1)), memPtr(src), bytes);
    texDirty = true;
    bindEpoch++;
}

void Recorder::endFrame() {
    if (frame->xfbCopies == 0 && frame->cmds.size() < (16u << 20)) {
        return;
    }
    frame->number = frameNumber++;
    static const bool perfLog = getenv("PETARI_PERFLOG") != nullptr;
    if (perfLog && frame->number % 600 == 0) {
        port_log("perf: recorded frame %llu: %u draws, %zu vertex words, %zu command words, %zu textures", (unsigned long long)frame->number,
                 frame->draws, frame->verts.size(), frame->cmds.size(), frame->textures.size());
        size_t entries = 0, bytes = 0;
        for (auto& kv : dlCache) {
            for (DlEntry& e : kv.second.ways) {
                entries++;
                bytes += e.bytes.capacity() + e.words.capacity() * 4 + e.draws.capacity() * sizeof(DlDraw);
            }
        }
        const DlStats& s = dlStats;
        port_log("perf: display lists per frame: %llu calls, %llu from the cache, %llu recorded; %llu KB of arrays hashed, %llu KB of vertices "
                 "copied, %llu KB decoded; cache %zu lists, %zu MB",
                 (unsigned long long)(s.calls / 600), (unsigned long long)(s.hits / 600), (unsigned long long)(s.recorded / 600),
                 (unsigned long long)(s.hashedBytes / 600 / 1024), (unsigned long long)(s.copiedWords * 4 / 600 / 1024),
                 (unsigned long long)(s.decodedWords * 4 / 600 / 1024), entries, bytes >> 20);
        dlStats = DlStats();
    }
    if (frameNumber % 120 == 0) {
        for (auto it = dlCache.begin(); it != dlCache.end();) {
            std::vector<DlEntry>& ways = it->second.ways;
            for (size_t i = 0; i < ways.size();) {
                if (ways[i].lastUsed + 600 < frameNumber) {
                    ways.erase(ways.begin() + (long)i);
                } else {
                    i++;
                }
            }
            if (ways.empty() && it->second.noDrawsUntil < frameNumber) {
                it = dlCache.erase(it);
            } else {
                ++it;
            }
        }
    }
    frame = frameQueue().publish(std::move(frame));
    resetBindings();
    snapshot();
    for (auto it = efbCopies.begin(); it != efbCopies.end();) {
        if (frameNumber - it->second.frame > 8) {
            it = efbCopies.erase(it);
        } else {
            ++it;
        }
    }
    if (texCache.size() > 1024) {
        for (auto it = texCache.begin(); it != texCache.end();) {
            if (frameNumber - it->second.checkedFrame > 600) {
                it = texCache.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void useRecorderBackend() {
    if (!sRecorder) {
        sRecorder = new Recorder();
    }
    setBackend(sRecorder);
}

}  // namespace gpu

extern "C" void port_gx_use_recorder_backend(void) { gpu::useRecorderBackend(); }

extern "C" uint64_t port_gx_recording_frame(void) { return gpu::sRecorder ? gpu::sRecorder->frameNumber : 0; }

extern "C" void port_gx_recorder_end_frame(void) {
    PortHostAllocScope hostAlloc;
    if (gpu::sRecorder && gpu::backend() == gpu::sRecorder) {
        gpu::sRecorder->endFrame();
    }
}

extern "C" void __PortGXFifoFlush(void);

extern "C" void port_gx_marker(uint32_t kind) {
    __PortGXFifoFlush();
    PortHostAllocScope hostAlloc;
    if (gpu::sRecorder && gpu::backend() == gpu::sRecorder) {
        gpu::sRecorder->cmds().push_back(gpu::CMD_MARKER);
        gpu::sRecorder->cmds().push_back(kind);
    }
}

extern "C" void port_gx_camera(const float* camera) {
    __PortGXFifoFlush();
    PortHostAllocScope hostAlloc;
    if (gpu::sRecorder && gpu::backend() == gpu::sRecorder) {
        gpu::WordBuffer& cmds = gpu::sRecorder->cmds();
        cmds.push_back(gpu::CMD_CAMERA);
        const uint32_t* w = (const uint32_t*)camera;
        cmds.insert(cmds.end(), w, w + gpu::kCameraWords);
    }
}
