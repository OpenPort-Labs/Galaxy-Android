// GX command stream parser: CP/XF/BP register loads, indexed XF loads,
// display-list calls and primitive draws.
#include <stdio.h>
#include <string.h>

#include "gpu.h"
#include "port/port.h"

extern "C" volatile uint32_t __PortGXPERegs[0x400];

namespace gpu {

GpuState g;
static Backend* sBackend;

void setBackend(Backend* b) { sBackend = b; }
Backend* backend() { return sBackend; }

const u8* memPtr(u32 addr) {
    if (addr >= 0x80000000u) {
        return (const u8*)(uintptr_t)addr;  // cached/uncached virtual
    }
    return (const u8*)port_phys_to_host(addr);
}

// ---------------------------------------------------------------------------
// Vertex size
// ---------------------------------------------------------------------------
static const u8 kCompSize[8] = {1, 1, 2, 2, 4, 0, 0, 0};    // u8 s8 u16 s16 f32
static const u8 kColorSize[8] = {2, 3, 4, 2, 3, 4, 0, 0};   // 565 888 888x 4444 6666 8888

static u32 computeVertexSize(int vat);

// The last result per vertex format (a draw call after another with the
// same format is the common case).
u32 vertexSize(int vat) {
    struct Entry {
        u32 lo, hi, a, b, c, size;
        bool valid;
    };
    static Entry cache[8];
    const CPState& cp = g.cp;
    Entry& e = cache[vat & 7];
    if (e.valid && e.lo == cp.vcdLo && e.hi == cp.vcdHi && e.a == cp.vatA[vat] && e.b == cp.vatB[vat] && e.c == cp.vatC[vat]) {
        return e.size;
    }
    e = Entry{cp.vcdLo, cp.vcdHi, cp.vatA[vat], cp.vatB[vat], cp.vatC[vat], computeVertexSize(vat), true};
    return e.size;
}

static u32 computeVertexSize(int vat) {
    const CPState& cp = g.cp;
    u32 lo = cp.vcdLo, hi = cp.vcdHi;
    u32 a = cp.vatA[vat], b = cp.vatB[vat], c = cp.vatC[vat];
    u32 size = 0;

    // Matrix indices (direct only).
    size += (lo & 1);                    // PNMTXIDX
    for (int i = 0; i < 8; i++) {
        size += (lo >> (1 + i)) & 1;     // TEXnMTXIDX
    }

    auto attrSize = [](u32 type, u32 directSize) -> u32 {
        switch (type) {
        case ATTR_DIRECT:
            return directSize;
        case ATTR_INDEX8:
            return 1;
        case ATTR_INDEX16:
            return 2;
        default:
            return 0;
        }
    };

    // Position
    {
        u32 type = (lo >> 9) & 3;
        u32 cnt = (a >> 0) & 1, fmt = (a >> 1) & 7;
        size += attrSize(type, (cnt ? 3 : 2) * kCompSize[fmt]);
    }
    // Normal (NBT: 9 components; with NORMALINDEX3 and indexed, 3 indices)
    {
        u32 type = (lo >> 11) & 3;
        u32 cnt = (a >> 9) & 1, fmt = (a >> 10) & 7;
        bool index3 = (a >> 31) & 1;
        if (type == ATTR_DIRECT) {
            size += (cnt ? 9 : 3) * kCompSize[fmt];
        } else if (type != ATTR_NONE) {
            u32 idx = (type == ATTR_INDEX8) ? 1 : 2;
            size += (cnt && index3) ? idx * 3 : idx;
        }
    }
    // Colors
    {
        u32 type0 = (lo >> 13) & 3, fmt0 = (a >> 14) & 7;
        u32 type1 = (lo >> 15) & 3, fmt1 = (a >> 18) & 7;
        size += attrSize(type0, kColorSize[fmt0]);
        size += attrSize(type1, kColorSize[fmt1]);
    }
    // Texture coordinates
    {
        u32 cnt[8], fmt[8];
        cnt[0] = (a >> 21) & 1; fmt[0] = (a >> 22) & 7;
        cnt[1] = (b >> 0) & 1;  fmt[1] = (b >> 1) & 7;
        cnt[2] = (b >> 9) & 1;  fmt[2] = (b >> 10) & 7;
        cnt[3] = (b >> 18) & 1; fmt[3] = (b >> 19) & 7;
        cnt[4] = (b >> 27) & 1; fmt[4] = (b >> 28) & 7;
        cnt[5] = (c >> 5) & 1;  fmt[5] = (c >> 6) & 7;
        cnt[6] = (c >> 14) & 1; fmt[6] = (c >> 15) & 7;
        cnt[7] = (c >> 23) & 1; fmt[7] = (c >> 24) & 7;
        for (int i = 0; i < 8; i++) {
            u32 type = (hi >> (i * 2)) & 3;
            size += attrSize(type, (cnt[i] ? 2 : 1) * kCompSize[fmt[i]]);
        }
    }
    return size;
}

// ---------------------------------------------------------------------------
// Register writes
// ---------------------------------------------------------------------------
static void writeCP(u8 reg, u32 value) {
    CPState& cp = g.cp;
    switch (reg & 0xF0) {
    case 0x30:
        cp.matIndexA = value;
        g.xf.regs[0x18] = value;
        break;
    case 0x40:
        cp.matIndexB = value;
        g.xf.regs[0x19] = value;
        break;
    case 0x50:
        cp.vcdLo = value;
        break;
    case 0x60:
        cp.vcdHi = value;
        break;
    case 0x70:
        cp.vatA[reg & 7] = value;
        break;
    case 0x80:
        cp.vatB[reg & 7] = value;
        break;
    case 0x90:
        cp.vatC[reg & 7] = value;
        break;
    case 0xA0:
        cp.arrayBase[reg & 0xF] = value;
        break;
    case 0xB0:
        cp.arrayStride[reg & 0xF] = value & 0xFF;
        break;
    default:
        break;
    }
    if (sBackend) {
        sBackend->cpWrite(reg, value);
    }
}

static void writeXF(u32 addr, u32 value) {
    if (addr >= 240 && addr <= 255) {
        return;  // Hardwired identity matrix ROM (GX_IDENTITY)
    }
    if (addr < 0x1000) {
        float f;
        memcpy(&f, &value, 4);
        g.xf.mem[addr] = f;
    } else if (addr < 0x1100) {
        g.xf.regs[addr - 0x1000] = value;
        if (addr == 0x1018) {
            g.cp.matIndexA = value;
        } else if (addr == 0x1019) {
            g.cp.matIndexB = value;
        }
    }
}

static inline void peReg16(int idx, u16 v) { ((volatile u16*)__PortGXPERegs)[idx] = v; }

static void writeBP(u32 cmd) {
    u32 reg = cmd >> 24;
    u32 value = cmd & 0xFFFFFF;
    if (reg == 0xFE) {
        g.bpMask = value;
        return;
    }
    u32 old = g.bp[reg];
    u32 newValue = (old & ~g.bpMask) | (value & g.bpMask);
    g.bpMask = 0xFFFFFF;
    g.bp[reg] = newValue;
    trackTevColorWrite(g.tevReg, g.tevKonst, reg, newValue);

    switch (reg) {
    case 0x45:  // PE_DONE: draw done
        if (newValue & 2) {
            port_irq_raise(PORT_IRQ_PE_FINISH);
        }
        break;
    case 0x47:  // PE_TOKEN_INT
        peReg16(7, (u16)newValue);
        port_irq_raise(PORT_IRQ_PE_TOKEN);
        break;
    case 0x48:  // PE_TOKEN
        peReg16(7, (u16)newValue);
        break;
    case 0x52:  // EFB copy trigger
        if (sBackend) {
            bool toXfb = (newValue >> 14) & 1;
            bool clear = (newValue >> 11) & 1;
            sBackend->efbCopy(!toXfb ? false : true, clear);
        }
        break;
    case 0x65:  // TLUT load trigger
        if (sBackend) {
            sBackend->loadTlut(newValue);
        }
        break;
    case 0x63:  // TMEM preload trigger
        if (sBackend) {
            sBackend->preloadTexture();
        }
        break;
    default:
        break;
    }
    if (sBackend) {
        sBackend->bpWrite(reg, newValue, old);
    }
}

// Indexed XF load (LOAD_INDX_A..D): copies `size` words from array 12..15.
static void loadIndexed(int array, u32 cmd) {
    u32 index = cmd >> 16;
    u32 size = ((cmd >> 12) & 0xF) + 1;
    u32 addr = cmd & 0xFFF;
    const u8* src = memPtr(g.cp.arrayBase[array] + index * g.cp.arrayStride[array]);
    // Array data is written by the CPU (matrices) and is little-endian on the host.
    for (u32 i = 0; i < size; i++) {
        u32 v;
        memcpy(&v, src + i * 4, 4);
        writeXF(addr + i, v);
    }
    if (sBackend) {
        sBackend->xfWrite(addr, size);
    }
}

// ---------------------------------------------------------------------------
// Stream parser
// ---------------------------------------------------------------------------

// Recent commands, dumped when the parser meets an unknown opcode.
struct TraceEntry {
    size_t pos;
    u8 op;
    u32 a, b;
};
static TraceEntry sTrace[16];
static unsigned sTraceHead;

static void trace(size_t pos, u8 op, u32 a, u32 b) {
    sTrace[sTraceHead++ & 15] = {pos, op, a, b};
}

static void dumpTrace(const u8* data, size_t start) {
    for (unsigned i = 0; i < 16; i++) {
        const TraceEntry& t = sTrace[(sTraceHead + i) & 15];
        port_log("gpu:   +0x%zx op %02x %08x %08x", t.pos, t.op, t.a, t.b);
    }
    const CPState& cp = g.cp;
    port_log("gpu:   vcd %08x %08x vat0 %08x %08x %08x", cp.vcdLo, cp.vcdHi, cp.vatA[0], cp.vatB[0], cp.vatC[0]);
    size_t from = start >= 48 ? start - 48 : 0;
    char line[200];
    size_t n = 0;
    for (size_t i = from; i < start + 16; i++) {
        n += snprintf(line + n, sizeof(line) - n, "%s%02x", i == start ? "[" : " ", data[i]);
        if (n > 150) {
            port_log("gpu:   %s", line);
            n = 0;
        }
    }
    if (n) {
        port_log("gpu:   %s", line);
    }
}

u64 gStreamSerial = 0;
u64 gDrawCount = 0;

size_t process(const u8* data, size_t len, bool isDisplayList) {
    if (!isDisplayList) {
        gStreamSerial++;
    }
    size_t pos = 0;
    while (pos < len) {
        size_t start = pos;
        u8 op = data[pos++];
        if (op != 0) {
            trace(start, op, pos + 4 <= len ? be32(data + pos) : 0, 0);
        }
        switch (op) {
        case 0x00:  // NOP
            break;
        case 0x08: {  // LOAD_CP_REG
            if (pos + 5 > len) {
                return start;
            }
            u8 reg = data[pos];
            u32 value = be32(data + pos + 1);
            pos += 5;
            writeCP(reg, value);
            break;
        }
        case 0x10: {  // LOAD_XF_REG
            if (pos + 4 > len) {
                return start;
            }
            u32 hdr = be32(data + pos);
            u32 count = ((hdr >> 16) & 0xF) + 1;
            u32 addr = hdr & 0xFFFF;
            if (pos + 4 + count * 4 > len) {
                return start;
            }
            pos += 4;
            for (u32 i = 0; i < count; i++) {
                writeXF(addr + i, be32(data + pos));
                pos += 4;
            }
            if (sBackend) {
                sBackend->xfWrite(addr, count);
            }
            break;
        }
        case 0x20:
        case 0x28:
        case 0x30:
        case 0x38: {  // LOAD_INDX_A..D
            if (pos + 4 > len) {
                return start;
            }
            loadIndexed(12 + ((op >> 3) & 3), be32(data + pos));
            pos += 4;
            break;
        }
        case 0x40: {  // CALL_DL
            if (pos + 8 > len) {
                return start;
            }
            u32 addr = be32(data + pos);
            u32 size = be32(data + pos + 4);
            pos += 8;
            if (isDisplayList) {
                port_log("gpu: nested display list call ignored");
            } else if (addr != 0 && size != 0) {
                const u8* list = memPtr(addr);
                if (!sBackend) {
                    process(list, size, true);
                } else if (!sBackend->displayListBegin(addr, list, size)) {
                    process(list, size, true);
                    sBackend->displayListEnd();
                }
            }
            break;
        }
        case 0x44:  // unknown (metrics)
        case 0x48:  // invalidate vertex cache
            break;
        case 0x61: {  // LOAD_BP_REG
            if (pos + 4 > len) {
                return start;
            }
            writeBP(be32(data + pos));
            pos += 4;
            break;
        }
        default:
            if (op & 0x80) {  // draw
                if (pos + 2 > len) {
                    return start;
                }
                u32 count = be16(data + pos);
                pos += 2;
                int vat = op & 7;
                u32 stride = vertexSize(vat);
                sTrace[(sTraceHead - 1) & 15].b = (count << 16) | stride;
                size_t bytes = (size_t)count * stride;
                if (pos + bytes > len) {
                    return start;
                }
                if (count && sBackend) {
                    gDrawCount++;
                    sBackend->draw(op & 0xF8, vat, count, data + pos, stride);
                }
                pos += bytes;
            } else {
                static int warned = 0;
                if (warned++ < 16) {
                    port_log("gpu: unknown opcode 0x%02x at +0x%zx (%s)", op, start, isDisplayList ? "DL" : "FIFO");
                    if (warned == 1) {
                        dumpTrace(data, start);
                    }
                }
                // Resynchronise by skipping the byte.
            }
            break;
        }
    }
    return pos;
}

}  // namespace gpu
