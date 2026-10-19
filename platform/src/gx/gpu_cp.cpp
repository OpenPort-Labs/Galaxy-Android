// GX command stream parser: CP/XF/BP register loads, indexed XF loads,
// display-list calls and primitive draws.
#include <stdio.h>
#include <string.h>

#include "gpu.h"
#include "gpu_cp_regs.h"
#include "port/port.h"

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
