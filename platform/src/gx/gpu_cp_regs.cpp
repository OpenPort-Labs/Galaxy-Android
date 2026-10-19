#include "gpu_cp_regs.h"
#include <string.h>
#include "port/port.h"

extern "C" volatile uint32_t __PortGXPERegs[0x400];

namespace gpu {

static const u8 kCompSize[8] = {1, 1, 2, 2, 4, 0, 0, 0};    // u8 s8 u16 s16 f32
static const u8 kColorSize[8] = {2, 3, 4, 2, 3, 4, 0, 0};   // 565 888 888x 4444 6666 8888

static u32 computeVertexSize(int vat);

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

    size += (lo & 1);
    for (int i = 0; i < 8; i++) {
        size += (lo >> (1 + i)) & 1;
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
    // Normal
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

void writeCP(u8 reg, u32 value) {
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
    if (backend()) {
        backend()->cpWrite(reg, value);
    }
}

void writeXF(u32 addr, u32 value) {
    if (addr >= 240 && addr <= 255) {
        return;
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

void writeBP(u32 cmd) {
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
    case 0x45:
        if (newValue & 2) {
            port_irq_raise(PORT_IRQ_PE_FINISH);
        }
        break;
    case 0x47:
        peReg16(7, (u16)newValue);
        port_irq_raise(PORT_IRQ_PE_TOKEN);
        break;
    case 0x48:
        peReg16(7, (u16)newValue);
        break;
    case 0x52:
        if (backend()) {
            bool toXfb = (newValue >> 14) & 1;
            bool clear = (newValue >> 11) & 1;
            backend()->efbCopy(!toXfb ? false : true, clear);
        }
        break;
    case 0x65:
        if (backend()) {
            backend()->loadTlut(newValue);
        }
        break;
    case 0x63:
        if (backend()) {
            backend()->preloadTexture();
        }
        break;
    default:
        break;
    }
    if (backend()) {
        backend()->bpWrite(reg, newValue, old);
    }
}

void loadIndexed(int array, u32 cmd) {
    u32 index = cmd >> 16;
    u32 size = ((cmd >> 12) & 0xF) + 1;
    u32 addr = cmd & 0xFFF;
    const u8* src = memPtr(g.cp.arrayBase[array] + index * g.cp.arrayStride[array]);
    for (u32 i = 0; i < size; i++) {
        u32 v;
        memcpy(&v, src + i * 4, 4);
        writeXF(addr + i, v);
    }
    if (backend()) {
        backend()->xfWrite(addr, size);
    }
}

}  // namespace gpu
