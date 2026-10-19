#include "gx_recorder_internal.h"

namespace gpu {

u32 Recorder::usedTexMaps() const {
    const u32* bp = g.bp;
    u32 genMode = bp[0x00];
    u32 numTev = ((genMode >> 10) & 15) + 1;
    u32 numInd = (genMode >> 16) & 7;
    u32 mask = 0;
    for (u32 s = 0; s < numTev; s++) {
        u32 order = bp[0x28 + s / 2] >> ((s & 1) * 12);
        if ((order >> 6) & 1) {
            mask |= 1u << (order & 7);
        }
    }
    u32 iref = bp[0x27];
    for (u32 s = 0; s < numInd; s++) {
        mask |= 1u << ((iref >> (s * 6)) & 7);
    }
    return mask;
}

void Recorder::resolveTextures() {
    if (!texDirty) {
        return;
    }
    texDirty = false;
    u32 mask = usedTexMaps();
    for (u32 unit = 0; unit < 8; unit++) {
        if (!(mask & (1u << unit))) {
            continue;
        }
        const u32* bp = g.bp;
        u32 rb = kTexRegBase[unit];
        UnitMemo& m = unitMemo[unit];
        u32 binding;
        if (m.epoch == bindEpoch && m.mode0 == bp[rb] && m.mode1 == bp[rb + 4] && m.image0 == bp[rb + 8] && m.image3 == bp[rb + 0x14] &&
            m.tlut == bp[rb + 0x18]) {
            binding = m.binding;
        } else {
            binding = resolveUnit(unit);
            m = UnitMemo{bp[rb], bp[rb + 4], bp[rb + 8], bp[rb + 0x14], bp[rb + 0x18], binding, bindEpoch};
        }
        if (binding != boundTex[unit]) {
            boundTex[unit] = binding;
            auto& c = cmds();
            if (binding & 0x80000000u) {
                const EfbCopyInfo* info = nullptr;
                u32 addr = 0;
                for (auto& kv : efbCopies) {
                    if (kv.second.id == (binding & 0x7FFFFFFF)) {
                        info = &kv.second;
                        addr = kv.first;
                        break;
                    }
                }
                c.push_back(CMD_TEX_EFB);
                c.push_back(unit);
                c.push_back(binding & 0x7FFFFFFF);
                c.push_back(info ? info->width : 1);
                c.push_back(info ? info->height : 1);
                c.push_back(addr);
            } else {
                c.push_back(CMD_TEX);
                c.push_back(unit);
                c.push_back(binding);
            }
        }
    }
}

u32 Recorder::resolveUnit(u32 unit) {
    const u32* bp = g.bp;
    u32 rb = kTexRegBase[unit];
    u32 mode0 = bp[rb], mode1 = bp[rb + 4], image0 = bp[rb + 8], image3 = bp[rb + 0x14], tlutReg = bp[rb + 0x18];
    u32 width = (image0 & 0x3FF) + 1, height = ((image0 >> 10) & 0x3FF) + 1, fmt = (image0 >> 20) & 15;
    u32 addr = (image3 & 0xFFFFFF) << 5;

    auto copy = efbCopies.find(addr);
    if (copy != efbCopies.end()) {
        if (hashBytes(memPtr(addr), 64) == copy->second.memHash) {
            return 0x80000000u | copy->second.id;
        }
        efbCopies.erase(copy);
    }

    u32 minFilter = (mode0 >> 5) & 7;
    u32 maxLod = (mode1 >> 8) & 0xFF;
    u32 levels = 1;
    if (minFilter != 0 && minFilter != 4) {
        levels = maxLod / 16 + 1;
        u32 maxLevels = 1;
        for (u32 w = width, h = height; w > 1 || h > 1; w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1) {
            maxLevels++;
        }
        if (levels > maxLevels) {
            levels = maxLevels;
        }
    }
    bool ci = fmt == TF_CI4 || fmt == TF_CI8 || fmt == TF_CI14X2;
    TexKey key{addr, fmt, width, height, levels, ci ? ((tlutReg & 0x3FF) << 9) : 0u, ci ? ((tlutReg >> 10) & 3) : 0u};
    TexEntry& e = texCache[key];

    size_t total = 0;
    for (u32 l = 0, w = width, h = height; l < levels; l++, w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1) {
        total += texLevelSize(fmt, w, h);
    }
    if (e.checkedFrame != frameNumber) {
        e.checkedFrame = frameNumber;
        const u8* src = memPtr(addr);
        u64 h = hashBytes(src, total);
        if (ci) {
            u32 entries = fmt == TF_CI4 ? 16 : fmt == TF_CI8 ? 256 : 16384;
            h ^= hashBytes(tlutMem + (key.tlutAddr & (sizeof(tlutMem) - 1)), entries * 2) * 31;
        }
        if (!e.image || h != e.dataHash) {
            e.dataHash = h;
            e.image = std::make_shared<TexImage>();
            decode(*e.image, src, key);
        }
    }

    auto it = frameTexIndex.find(e.image.get());
    if (it != frameTexIndex.end()) {
        return it->second;
    }
    u32 index = (u32)frame->textures.size();
    frame->textures.push_back(e.image);
    frameTexIndex[e.image.get()] = index;
    return index;
}

void Recorder::decode(TexImage& img, const u8* src, const TexKey& key) {
    size_t texels = 0;
    for (u32 l = 0, w = key.width, h = key.height; l < key.levels; l++, w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1) {
        texels += (size_t)w * h;
    }
    img.rgba.resize(texels);
    img.width = key.width;
    img.height = key.height;
    img.levels = key.levels;
    const u16* tlut = (const u16*)(tlutMem + (key.tlutAddr & (sizeof(tlutMem) - 1)));
    u32* dst = img.rgba.data();
    const u8* p = src;
    for (u32 l = 0, w = key.width, h = key.height; l < key.levels; l++, w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1) {
        decodeTexture((u8*)dst, p, key.fmt, w, h, tlut, key.tlutFmt);
        dst += (size_t)w * h;
        p += texLevelSize(key.fmt, w, h);
    }

    static bool sCustomTexChecked = false;
    static bool sCustomTexFolderExists = false;
    if (!sCustomTexChecked) {
        sCustomTexChecked = true;
        struct stat st;
        sCustomTexFolderExists = (stat("/sdcard/GalaxyData/textures", &st) == 0 && S_ISDIR(st.st_mode));
        if (sCustomTexFolderExists) {
            port_log("textures: Custom texture overrides enabled from /sdcard/GalaxyData/textures");
        }
    }
    if (sCustomTexFolderExists) {
        char customPath[128];
        snprintf(customPath, sizeof(customPath), "/sdcard/GalaxyData/textures/tex_%08x_%dx%d.rgba", key.addr, key.width, key.height);
        FILE* cf = fopen(customPath, "rb");
        if (cf) {
            fseek(cf, 0, SEEK_END);
            long sz = ftell(cf);
            fseek(cf, 0, SEEK_SET);
            if (sz == (long)(texels * 4)) {
                if (fread(img.rgba.data(), 1, sz, cf) == (size_t)sz) {
                    port_log("textures: Loaded custom replacement texture %s", customPath);
                }
            }
            fclose(cf);
        }
    }

    img.version++;
}

}  // namespace gpu
