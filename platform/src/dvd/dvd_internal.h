#pragma once
#include <string>
#include <vector>

#include "port/port.h"
#include "revolution/dvd.h"

struct FstEntry {
    bool isDir;
    std::string name;
    u32 parent;
    u32 next;
    std::string hostPath;
    s64 size = -1;
    int fd = -1;
};

extern std::vector<FstEntry> sFst;
extern std::string sDataRoot;
extern u32 sCurrentDir;

u32 dvd_be32(const u8* p);
std::vector<u8> dvd_read_fst(const std::string& root);
bool dvd_fst_has_root_dir(const std::vector<u8>& fst, const char* name);
bool dvd_load_fst();
s64 dvd_entry_size(u32 entry);
s32 dvd_find_child(u32 dir, const char* name, size_t len);
