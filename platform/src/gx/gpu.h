// Emulated Flipper/Hollywood GPU front end: register state + command processor.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

namespace gpu {

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// Value of a debug environment variable, or nullptr when unset or empty.
inline const char* debugEnv(const char* name) {
    const char* v = getenv(name);
    return v && *v ? v : nullptr;
}
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

inline u16 be16(const u8* p) { return (u16)((p[0] << 8) | p[1]); }
inline u32 be32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
inline float befloat(const u8* p) {
    u32 v = be32(p);
    float f;
    __builtin_memcpy(&f, &v, 4);
    return f;
}

// Command-processor (CP) register file relevant to vertex fetch.
struct CPState {
    u32 matIndexA = 0x27921780;  // 0x30: pos=0, tex0=30, tex1=33, tex2=36, tex3=39 (Hardware GX defaults)
    u32 matIndexB = 0x00cf0b6a;  // 0x40: tex4=42, tex5=45, tex6=48, tex7=51
    u32 vcdLo = 0;      // 0x50
    u32 vcdHi = 0;      // 0x60
    u32 vatA[8] = {};   // 0x70+n
    u32 vatB[8] = {};   // 0x80+n
    u32 vatC[8] = {};   // 0x90+n
    u32 arrayBase[16] = {};    // 0xA0+n (physical or virtual address)
    u32 arrayStride[16] = {};  // 0xB0+n
};

// Transform-unit (XF) memory and registers.
struct XFState {
    float mem[0x1000];  // 0x0000-0x0FFF: matrices, normal matrices, dual tex, lights
    u32 regs[0x100];    // 0x1000-0x10FF
};

struct GpuState {
    CPState cp;
    XFState xf;
    u32 bp[0x100] = {};
    u32 bpMask = 0xFFFFFF;
    u32 tevReg[8] = {};    // PREV/REG0-2: lo (R,A), hi (B,G) pairs
    u32 tevKonst[8] = {};  // K0-K3

    GpuState() {
        // Default GX swap tables (GXInit.c / Nintendo GX specification):
        // Table 0: R=0, G=1, B=2, A=3 -> lo: (1<<2)|0 = 0x04, hi: (3<<2)|2 = 0x0E (.rgba)
        bp[0xF6] = 0x000004;
        bp[0xF7] = 0x00000E;
        // Table 1: R=0, G=0, B=0, A=3 -> lo: 0x00, hi: (3<<2)|0 = 0x0C (.rrra)
        bp[0xF8] = 0x000000;
        bp[0xF9] = 0x00000C;
        // Table 2: G=1, G=1, G=1, A=3 -> lo: (1<<2)|1 = 0x05, hi: (3<<2)|1 = 0x0D (.ggga)
        bp[0xFA] = 0x000005;
        bp[0xFB] = 0x00000D;
        // Table 3: B=2, B=2, B=2, A=3 -> lo: (2<<2)|2 = 0x0A, hi: (3<<2)|2 = 0x0E (.bbba)
        bp[0xFC] = 0x00000A;
        bp[0xFD] = 0x00000E;

        // Default GX channel colors (GXInit.c):
        // 0x100A / 0x100B: Ambient 0 & 1 (black: RGBA 0, 0, 0, 255)
        xf.regs[0x0A] = 0x000000FF;
        xf.regs[0x0B] = 0x000000FF;
        // 0x100C / 0x100D: Material 0 & 1 (white: RGBA 255, 255, 255, 255)
        xf.regs[0x0C] = 0xFFFFFFFF;
        xf.regs[0x0D] = 0xFFFFFFFF;
        // Default GX matrix indices (GXInit.c defaults: pos=0, tex0=30, tex1=33, tex2=36, etc.):
        xf.regs[0x18] = 0x27921780;  // 0x1018 MatrixIndex0: pos=0, tex0..3=30..39
        xf.regs[0x19] = 0x00cf0b6a;  // 0x1019 MatrixIndex1: tex4..7=42..51

        // Hardwired XF identity matrix at rows 60..63 (GX_IDENTITY = 60 = 0x3C):
        // Pos/Tex matrix memory (rows 60..63, addresses 240..255):
        xf.mem[240] = 1.0f; xf.mem[241] = 0.0f; xf.mem[242] = 0.0f; xf.mem[243] = 0.0f;
        xf.mem[244] = 0.0f; xf.mem[245] = 1.0f; xf.mem[246] = 0.0f; xf.mem[247] = 0.0f;
        xf.mem[248] = 0.0f; xf.mem[249] = 0.0f; xf.mem[250] = 1.0f; xf.mem[251] = 0.0f;
        xf.mem[252] = 0.0f; xf.mem[253] = 0.0f; xf.mem[254] = 0.0f; xf.mem[255] = 1.0f;

        // Post-transform matrix memory rows 60..63 (addresses 1520..1535):
        xf.mem[1520] = 1.0f; xf.mem[1521] = 0.0f; xf.mem[1522] = 0.0f; xf.mem[1523] = 0.0f;
        // GX_PTIDENTITY (125 - 64 = 61): 3x4 identity matrix starting at row 61 (indices 1524..1535):
        xf.mem[1524] = 1.0f; xf.mem[1525] = 0.0f; xf.mem[1526] = 0.0f; xf.mem[1527] = 0.0f;
        xf.mem[1528] = 0.0f; xf.mem[1529] = 1.0f; xf.mem[1530] = 0.0f; xf.mem[1531] = 0.0f;
        xf.mem[1532] = 0.0f; xf.mem[1533] = 0.0f; xf.mem[1534] = 1.0f; xf.mem[1535] = 0.0f;
    }
};

// Tracks the TEV color/konst register a BP 0xE0-0xE7 write goes to.
inline void trackTevColorWrite(u32* tevReg, u32* tevKonst, u32 reg, u32 value) {
    if (reg >= 0xE0 && reg <= 0xE7) {
        if (value & 0x800000) {
            tevKonst[reg - 0xE0] = value;
        } else {
            tevReg[reg - 0xE0] = value;
        }
    }
}

extern GpuState g;

// Vertex descriptor decoding.
enum AttrType : u8 { ATTR_NONE = 0, ATTR_DIRECT = 1, ATTR_INDEX8 = 2, ATTR_INDEX16 = 3 };

// Size in bytes of one vertex for vertex format `vat` given the current VCD.
u32 vertexSize(int vat);

// Backend interface implemented by the renderer.
struct Backend {
    virtual ~Backend() = default;
    // A primitive batch: `data` points at `count` vertices in the command stream.
    virtual void draw(int primitive, int vat, u32 count, const u8* data, u32 vertexStride) = 0;
    virtual void bpWrite(u32 reg, u32 value, u32 oldValue) = 0;  // after g.bp updated
    virtual void xfWrite(u32 addr, u32 count) = 0;               // after g.xf updated
    virtual void cpWrite(u32 reg, u32 value) = 0;
    virtual void efbCopy(bool toXfb, bool clear) = 0;            // BP 0x52 trigger
    virtual void loadTlut(u32 regValue) = 0;                     // BP 0x65 trigger
    virtual void preloadTexture() = 0;                           // BP 0x63 (TMEM preload)
    virtual u32 peekColor(u16 x, u16 y) = 0;
    virtual u32 peekZ(u16 x, u16 y) = 0;
    // Bracket a display list call (CALL_DL at `addr`, `size` bytes at
    // `data`): the draws in between come from the list.  If Begin returns
    // true, the backend has already replayed the whole list: it is not
    // parsed and End is not called.
    virtual bool displayListBegin(u32 addr, const u8* data, u32 size) { return false; }
    virtual void displayListEnd() {}
};

void setBackend(Backend* b);
Backend* backend();

// Primitive batches the command processor has dispatched to the active backend
// (whichever one that is), for progress logging.
extern u64 gDrawCount;

// Processes a complete command stream (FIFO contents or a display list).
// Each FIFO stream processed bumps gStreamSerial.
// Returns the number of bytes consumed (a trailing incomplete command is left).
size_t process(const u8* data, size_t len, bool isDisplayList);
extern u64 gStreamSerial;

// Host pointer for an address written into GPU registers (physical, cached
// or uncached virtual).
const u8* memPtr(u32 addr);

}  // namespace gpu
