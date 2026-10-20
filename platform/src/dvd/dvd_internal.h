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

enum DvdIoState {
    kStateEnd = 0,
    kStateBusy = 1,
    kStateWaiting = 2,
    kStateCanceled = 10,
    kStateFatal = -1,
};

inline u32 dvd_be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
std::vector<u8> dvd_read_fst(const std::string& root);
bool dvd_fst_has_root_dir(const std::vector<u8>& fst, const char* name);
bool dvd_load_fst();
void dvd_fst_clear();
s64 dvd_entry_size(u32 entry);
s32 dvd_find_child(u32 dir, const char* name, size_t len);

// Asynchronous read engine (dvd_read.cpp): queue, worker thread, and the range
// validation that gates every request.  dvd.cpp owns the interrupt handler and
// the public DVD API.
void dvd_io_start();
void dvd_io_stop();
BOOL dvd_io_submit(DVDCommandBlock* block, u32 entry);
int dvd_io_cancel(DVDCommandBlock* block);  // 1: dequeued from queue, 2: active, 0: not found
bool dvd_io_pop_done(DVDCommandBlock** block);
bool dvd_io_idle();
bool dvd_io_validate(const DVDFileInfo* fileInfo, const void* addr, s32 length, s32 offset, const char* api);
