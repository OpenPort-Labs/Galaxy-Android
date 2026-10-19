#include "headless_dump.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "gpu.h"
#include "port/port.h"

namespace {

unsigned crcTable[256];
void initCrc() {
    for (unsigned n = 0; n < 256; n++) {
        unsigned c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}

unsigned crc(const unsigned char* p, size_t n, unsigned c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; i++) c = crcTable[(c ^ p[i]) & 255] ^ (c >> 8);
    return c;
}

void put32(std::vector<unsigned char>& v, unsigned x) {
    v.push_back(x >> 24);
    v.push_back(x >> 16);
    v.push_back(x >> 8);
    v.push_back(x);
}

void chunk(FILE* f, const char* type, const std::vector<unsigned char>& data) {
    std::vector<unsigned char> buf;
    put32(buf, (unsigned)data.size());
    buf.insert(buf.end(), type, type + 4);
    buf.insert(buf.end(), data.begin(), data.end());
    unsigned c = crc(buf.data() + 4, buf.size() - 4) ^ 0xFFFFFFFFu;
    put32(buf, c);
    fwrite(buf.data(), 1, buf.size(), f);
}

}  // namespace

bool headless_write_png(const char* path, const unsigned char* rgba, int w, int h) {
    static bool crcReady;
    if (!crcReady) {
        initCrc();
        crcReady = true;
    }
    FILE* f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    std::vector<unsigned char> ihdr;
    put32(ihdr, w);
    put32(ihdr, h);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(f, "IHDR", ihdr);

    // Raw scanlines (filter 0), rows flipped (GL origin is bottom-left).
    std::vector<unsigned char> raw;
    raw.reserve((size_t)(w * 4 + 1) * h);
    for (int y = h - 1; y >= 0; y--) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + (size_t)y * w * 4, rgba + (size_t)(y + 1) * w * 4);
    }
    std::vector<unsigned char> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = raw.size() - pos;
        if (n > 65535) n = 65535;
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(n & 255);
        z.push_back(n >> 8);
        z.push_back(~n & 255);
        z.push_back((~n >> 8) & 255);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    unsigned a = 1, b = 0;
    for (unsigned char c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    put32(z, (b << 16) | a);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    fclose(f);
    return true;
}

extern "C" void port_headless_write_png(const char* path, const unsigned char* rgba, int w, int h) {
    headless_write_png(path, rgba, w, h);
}

void headless_dump_frame(const gpu::Frame& f, const char* dir) {
    using namespace gpu;
    u32 bp[256], xf[256], treg[8], tk[8];
    memcpy(treg, f.startTevReg, sizeof(treg));
    memcpy(tk, f.startTevKonst, sizeof(tk));
    memcpy(bp, f.startBp, sizeof(bp));
    memcpy(xf, f.startXfRegs, sizeof(xf));
    const u32* c = f.cmds.data();
    const u32* end = c + f.cmds.size();
    int draws = 0;
    while (c < end) {
        u32 op = *c++;
        switch (op) {
        case CMD_XF: {
            u32 addr = c[0], count = c[1];
            for (u32 i = 0; i < count; i++) {
                if (addr + i >= 0x1000 && addr + i < 0x1100) xf[addr + i - 0x1000] = c[2 + i];
            }
            c += 2 + count;
            break;
        }
        case CMD_BP:
            bp[*c >> 24] = *c & 0xFFFFFF;
            trackTevColorWrite(treg, tk, *c >> 24, *c & 0xFFFFFF);
            if (draws < 3) {
                port_log("dump:   bp %02x = %06x", *c >> 24, *c & 0xFFFFFF);
            }
            c++;
            break;
        case CMD_TEX:
            port_log("dump:   tex unit %u <- image %u", c[0], c[1]);
            c += 2;
            break;
        case CMD_TEX_EFB:
            port_log("dump:   tex unit %u <- efb copy %u (%ux%u at %08x)", c[0], c[1], c[2], c[3], c[4]);
            c += 5;
            break;
        case CMD_EFB_COPY:
            port_log("dump:   efb copy id %u src %06x/%06x dst %06x ctrl %06x clear %06x %06x", c[0], c[1], c[2], c[3], c[9], c[6], c[7]);
            c += 10;
            break;
        case CMD_MARKER:
            port_log("dump:   marker %u", *c++);
            break;
        case CMD_DRAW: {
            u32 prim = c[0], flags = c[1], first = c[2], count = c[3];
            c += 4;
            if (draws++ < 40) {
                float p[3];
                memcpy(p, &f.verts[first], 12);
                port_log("dump: draw %d prim %02x flags %05x count %u v0 (%.2f %.2f %.2f) tev %u gen %06x cc0 %06x ac0 %06x order %06x proj %u "
                         "cmode %06x zmode %06x numtex %u",
                         draws - 1, prim, flags, count, p[0], p[1], p[2], ((bp[0] >> 10) & 15) + 1, bp[0], bp[0xC0], bp[0xC1], bp[0x28],
                         xf[0x26] & 1, bp[0x41], bp[0x40], xf[0x3F]);
                u32 words = vtxWords(flags);
                for (u32 v = 0; v < count && v < 4; v++) {
                    char line[512];
                    int n = 0;
                    for (u32 w = 0; w < words && w < 16 && n < 480; w++) {
                        u32 x = f.verts[first + v * words + w];
                        float fx;
                        memcpy(&fx, &x, 4);
                        n += snprintf(line + n, sizeof(line) - n, " %08x(%.3g)", x, fx);
                    }
                    port_log("dump:   v%u:%s", v, line);
                }
                float pr[6], vpv[6];
                for (int i = 0; i < 6; i++) {
                    memcpy(&pr[i], &xf[0x20 + i], 4);
                    memcpy(&vpv[i], &xf[0x1A + i], 4);
                }
                port_log("dump:   texgen %05x %05x %05x post %03x %03x dual %u mtxidx %08x %08x chans %u cc %04x %04x ac %04x", xf[0x40], xf[0x41], xf[0x42],
                         xf[0x50], xf[0x51], xf[0x12], xf[0x18], xf[0x19], xf[0x09], xf[0x0E], xf[0x0F], xf[0x10]);
                port_log("dump:   tevreg %06x %06x | %06x %06x | %06x %06x | %06x %06x  konst %06x %06x %06x %06x", treg[0], treg[1],
                         treg[2], treg[3], treg[4], treg[5], treg[6], treg[7], tk[0], tk[1], tk[2], tk[3]);
                port_log("dump:   proj %g %g %g %g %g %g  vp %g %g %g %g %g %g  mtxidx %06x", pr[0], pr[1], pr[2], pr[3], pr[4], pr[5], vpv[0],
                         vpv[1], vpv[2], vpv[3], vpv[4], vpv[5], xf[0x18]);
            }
            break;
        }
        default:
            c = end;
            break;
        }
    }
    size_t maxTextures = atoi(getenv("PETARI_DUMP") ? getenv("PETARI_DUMP") : "") > 1 ? (size_t)atoi(getenv("PETARI_DUMP")) : 16;
    for (size_t i = 0; i < f.textures.size() && i < maxTextures; i++) {
        const TexImage& t = *f.textures[i];
        if (t.rgba.size() < (size_t)t.width * t.height) {
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s_tex%02zu.png", dir, i);
        std::vector<unsigned char> px((size_t)t.width * t.height * 4);
        for (u32 y = 0; y < t.height; y++) {
            memcpy(&px[(size_t)(t.height - 1 - y) * t.width * 4], &t.rgba[(size_t)y * t.width], t.width * 4);
        }
        headless_write_png(path, px.data(), (int)t.width, (int)t.height);
        port_log("dump: texture %zu: %ux%u levels %u -> %s", i, t.width, t.height, t.levels, path);
    }
}

extern "C" void port_headless_dump(const char* prefix) {
    auto frame = gpu::frameQueue().latest();
    if (frame) {
        headless_dump_frame(*frame, prefix);
    }
}
