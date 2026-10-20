#include "dvd_internal.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include "port/heap_routing.h"

std::vector<FstEntry> sFst;
std::string sDataRoot;
u32 sCurrentDir = 0;

void dvd_fst_clear() {
    for (auto& fe : sFst) {
        if (fe.fd >= 0) {
            close(fe.fd);
            fe.fd = -1;
        }
    }
    sFst.clear();
}

// The FST is attacker-controlled input (it comes out of a user-supplied disc
// image), so every count, offset and span below is proven against the buffer
// before it is used for pointer arithmetic or indexing.
static bool fst_reject(size_t size, const char* what, size_t value) {
    port_log("dvd: fst rejected (%s: %zu), %zu bytes", what, value, size);
    dvd_fst_clear();
    return false;
}

bool dvd_load_fst() {
    PortHostAllocScope scope;
    std::vector<u8> data = dvd_read_fst(sDataRoot);
    if (data.empty()) {
        port_log("dvd: cannot read %s/sys/fst.bin", sDataRoot.c_str());
        dvd_fst_clear();
        return false;
    }

    const size_t size = data.size();
    if (size < 12) {
        return fst_reject(size, "truncated header", size);
    }
    const size_t count = dvd_be32(&data[8]);
    if (count < 1) {
        return fst_reject(size, "zero entries", count);
    }
    // Division instead of count*12 keeps this overflow-safe on 32-bit hosts.
    if (count > size / 12) {
        return fst_reject(size, "entry table overruns the buffer", count);
    }
    // Root entry (entry 0) must be a directory with parent 0 and name offset 0.
    if (data[0] != 1 || (dvd_be32(&data[0]) & 0x00FFFFFF) != 0 || dvd_be32(&data[4]) != 0) {
        return fst_reject(size, "invalid root directory entry", 0);
    }

    const u8* strings = &data[count * 12];
    const size_t stringsSize = size - count * 12;
    dvd_fst_clear();
    sFst.resize(count);
    std::vector<std::string> dirPath(count);
    std::vector<u32> stack;
    stack.push_back(0);
    sFst[0].isDir = true;
    sFst[0].parent = 0;
    sFst[0].next = (u32)count;
    for (u32 i = 1; i < (u32)count; i++) {
        while (stack.size() > 1 && i >= sFst[stack.back()].next) {
            stack.pop_back();
        }
        const u8* e = &data[(size_t)i * 12];
        FstEntry& fe = sFst[i];
        fe.isDir = e[0] != 0;
        u32 nameOff = dvd_be32(e) & 0x00FFFFFF;
        if (nameOff >= stringsSize || !memchr(strings + nameOff, '\0', stringsSize - nameOff)) {
            return fst_reject(size, "out-of-range name offset", nameOff);
        }
        const char* name = (const char*)(strings + nameOff);
        if (*name == '\0' || strchr(name, '/') || strchr(name, '\\') || strchr(name, ':') ||
            strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            return fst_reject(size, "unusable name", i);
        }
        fe.name = name;
        u32 parentDir = stack.back();
        std::string full = dirPath[parentDir].empty() ? fe.name : dirPath[parentDir] + "/" + fe.name;
        if (fe.isDir) {
            u32 parent = dvd_be32(e + 4);
            u32 next = dvd_be32(e + 8);
            if (parent >= i || next <= i || next > (u32)count || next > sFst[parentDir].next) {
                return fst_reject(size, "invalid dir parent/next span", i);
            }
            if (parent != parentDir) {
                return fst_reject(size, "inconsistent dir parent", i);
            }
            if (stack.size() >= 32) {
                return fst_reject(size, "pathological directory depth", i);
            }
            fe.parent = parent;
            fe.next = next;
            dirPath[i] = full;
            stack.push_back(i);
        } else {
            fe.hostPath = sDataRoot + "/files/" + full;
        }
    }
    port_log("dvd: fst loaded, %zu entries, root %s", count, sDataRoot.c_str());
    return true;
}

s64 dvd_entry_size(u32 entry) {
    if (entry >= sFst.size() || sFst[entry].isDir) {
        // Directories have no payload; a stale entry number must not stat an
        // unrelated path or report a bogus size to DVDFastOpen.
        return 0;
    }
    FstEntry& fe = sFst[entry];
    if (fe.size < 0) {
#if defined(_WIN32)
        DWORD attr = GetFileAttributesA(fe.hostPath.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
            port_log("dvd: refusing to stat symlink %s", fe.hostPath.c_str());
            return 0;
        }
#else
        struct stat lst;
        if (lstat(fe.hostPath.c_str(), &lst) == 0 && S_ISLNK(lst.st_mode)) {
            port_log("dvd: refusing to stat symlink %s", fe.hostPath.c_str());
            return 0;
        }
#endif
        struct stat st;
        fe.size = (stat(fe.hostPath.c_str(), &st) == 0) ? (s64)st.st_size : 0;
        if (fe.size == 0) {
            port_log("dvd: missing file %s", fe.hostPath.c_str());
        }
    }
    return fe.size;
}

s32 dvd_find_child(u32 dir, const char* name, size_t len) {
    if (dir >= sFst.size() || !sFst[dir].isDir || name == nullptr) {
        return -1;
    }
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
    if (!path || sFst.empty()) {
        return -1;
    }
    u32 dir = (path[0] == '/') ? 0 : sCurrentDir;
    if (dir >= sFst.size() || !sFst[dir].isDir) {
        return -1;
    }
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
        if (found < 0 || (u32)found >= sFst.size()) {
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

bool dvd_fst_has_root_dir(const std::vector<u8>& fst, const char* name) {
    if (fst.size() < 12 || name == nullptr || fst[0] != 1) {
        return false;
    }
    u32 count = dvd_be32(&fst[8]);
    if (count < 1 || (size_t)count > fst.size() / 12) {
        return false;
    }
    const char* strings = (const char*)&fst[(size_t)count * 12];
    size_t stringsSize = fst.size() - (size_t)count * 12;
    size_t len = strlen(name);
    for (u32 i = 1; i < count;) {
        const u8* e = &fst[i * 12];
        bool isDir = e[0] != 0;
        u32 nameOff = dvd_be32(e) & 0x00FFFFFF;
        if (isDir && (size_t)nameOff + len + 1 <= stringsSize && strncasecmp(strings + nameOff, name, len + 1) == 0) {
            return true;
        }
        u32 next = dvd_be32(e + 8);
        if (isDir && (next <= i || next > count)) {
            return false;
        }
        i = (isDir && next > i) ? next : i + 1;
    }
    return false;
}
