#include "dvd_internal.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "port/heap_routing.h"

std::vector<FstEntry> sFst;
std::string sDataRoot;
u32 sCurrentDir = 0;

bool dvd_load_fst() {
    PortHostAllocScope scope;
    std::vector<u8> data = dvd_read_fst(sDataRoot);
    if (data.empty()) {
        port_log("dvd: cannot read %s/sys/fst.bin", sDataRoot.c_str());
        return false;
    }

    u32 count = dvd_be32(&data[8]);
    if (count < 1) {
        port_log("dvd: fst holds no entries");
        return false;
    }
    const u8* strings = &data[(size_t)count * 12];
    const size_t stringsSize = data.size() - (size_t)count * 12;
    sFst.resize(count);
    std::vector<std::string> dirPath(count);
    dirPath[0] = "";
    std::vector<u32> stack;
    stack.push_back(0);
    sFst[0].isDir = true;
    sFst[0].parent = 0;
    sFst[0].next = count;
    for (u32 i = 1; i < count; i++) {
        while (stack.size() > 1 && i >= sFst[stack.back()].next) {
            stack.pop_back();
        }
        const u8* e = &data[i * 12];
        FstEntry& fe = sFst[i];
        fe.isDir = e[0] != 0;
        u32 nameOff = dvd_be32(e) & 0x00FFFFFF;
        if (nameOff >= stringsSize || !memchr(strings + nameOff, '\0', stringsSize - nameOff)) {
            port_log("dvd: fst entry %u has an out-of-range name offset", i);
            return false;
        }
        const char* name = (const char*)(strings + nameOff);
        if (*name == '\0' || strchr(name, '/') || strchr(name, '\\') || strcmp(name, "..") == 0) {
            port_log("dvd: fst entry %u has an unusable name", i);
            return false;
        }
        fe.name = name;
        u32 parentDir = stack.back();
        std::string full = dirPath[parentDir].empty() ? fe.name : dirPath[parentDir] + "/" + fe.name;
        if (fe.isDir) {
            u32 parent = dvd_be32(e + 4);
            u32 next = dvd_be32(e + 8);
            if (parent >= count || next > count) {
                port_log("dvd: fst dir entry %u has an invalid parent/next span", i);
                return false;
            }
            fe.parent = parent;
            fe.next = next;
            dirPath[i] = full;
            stack.push_back(i);
        } else {
            fe.hostPath = sDataRoot + "/files/" + full;
        }
    }
    port_log("dvd: fst loaded, %u entries, root %s", count, sDataRoot.c_str());
    return true;
}

s64 dvd_entry_size(u32 entry) {
    FstEntry& fe = sFst[entry];
    if (fe.size < 0) {
        struct stat st;
        fe.size = (stat(fe.hostPath.c_str(), &st) == 0) ? (s64)st.st_size : 0;
        if (fe.size == 0) {
            port_log("dvd: missing file %s", fe.hostPath.c_str());
        }
    }
    return fe.size;
}

s32 dvd_find_child(u32 dir, const char* name, size_t len) {
    for (u32 i = dir + 1; i < sFst[dir].next;) {
        const FstEntry& fe = sFst[i];
        if (fe.name.size() == len && strncasecmp(fe.name.c_str(), name, len) == 0) {
            return (s32)i;
        }
        i = fe.isDir ? fe.next : i + 1;
    }
    return -1;
}

extern "C" s32 DVDConvertPathToEntrynum(const char* path) {
    u32 dir = (path[0] == '/') ? 0 : sCurrentDir;
    const char* p = path;
    while (*p == '/') {
        p++;
    }
    while (*p) {
        const char* end = p;
        while (*end && *end != '/') {
            end++;
        }
        size_t len = (size_t)(end - p);
        s32 found;
        if (len == 1 && p[0] == '.') {
            found = (s32)dir;
        } else if (len == 2 && p[0] == '.' && p[1] == '.') {
            found = (s32)sFst[dir].parent;
        } else {
            found = dvd_find_child(dir, p, len);
        }
        if (found < 0) {
            return -1;
        }
        if (*end == 0) {
            return found;
        }
        if (!sFst[found].isDir) {
            return -1;
        }
        dir = (u32)found;
        p = end + 1;
        while (*p == '/') {
            p++;
        }
    }
    return (s32)dir;
}
