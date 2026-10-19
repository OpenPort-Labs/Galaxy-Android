#include "dvd_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "port/heap_routing.h"
#include "port/port.h"
#include "revolution/dvd.h"
#include "revolution/os.h"

enum {
    kStateEnd = 0,
    kStateBusy = 1,
    kStateWaiting = 2,
    kStateCanceled = 10,
    kStateFatal = -1,
};

struct ReadRequest {
    DVDCommandBlock* block;
    u32 entry;
};

static std::mutex sQueueMutex;
static std::condition_variable sQueueCv;
static std::deque<ReadRequest> sPending;
static std::deque<DVDCommandBlock*> sDone;
static DVDCommandBlock* sActive = nullptr;
static OSThreadQueue sSyncQueue;

static int openEntry(u32 entry) {
    FstEntry& fe = sFst[entry];
    if (fe.fd < 0) {
        fe.fd = open(fe.hostPath.c_str(), O_RDONLY | O_CLOEXEC);
        if (fe.fd < 0) {
            port_log("dvd: open %s failed: %s", fe.hostPath.c_str(), strerror(errno));
        }
    }
    return fe.fd;
}

static void workerMain() {
    for (;;) {
        ReadRequest req;
        {
            std::unique_lock<std::mutex> lk(sQueueMutex);
            sQueueCv.wait(lk, [] { return !sPending.empty(); });
            req = sPending.front();
            sPending.pop_front();
            sActive = req.block;
        }
        DVDCommandBlock* b = req.block;
        s32 result;
        int fd = openEntry(req.entry);
        if (fd < 0) {
            result = -1;
        } else {
            u8* dst = (u8*)b->addr;
            size_t want = b->length;
            ssize_t got = pread(fd, dst, want, (off_t)b->offset);
            if (got < 0) {
                result = -1;
            } else {
                if ((size_t)got < want) {
                    memset(dst + got, 0, want - (size_t)got);
                }
                b->transferredSize = b->length;
                result = (s32)b->length;
            }
        }
        {
            std::lock_guard<std::mutex> lk(sQueueMutex);
            b->currTransferSize = (u32)result;
            sActive = nullptr;
            sDone.push_back(b);
        }
        port_irq_raise(PORT_IRQ_DVD);
    }
}

static void dvdIrq() {
    for (;;) {
        DVDCommandBlock* b;
        {
            std::lock_guard<std::mutex> lk(sQueueMutex);
            if (sDone.empty()) {
                break;
            }
            b = sDone.front();
            sDone.pop_front();
        }
        s32 result = (s32)b->currTransferSize;
        b->state = result < 0 ? kStateFatal : kStateEnd;
        if (b->callback) {
            b->callback(result, b);
        }
    }
    OSWakeupThread(&sSyncQueue);
}

static void submit(DVDCommandBlock* b, u32 entry) {
    b->state = kStateBusy;
    b->transferredSize = 0;
    std::lock_guard<std::mutex> lk(sQueueMutex);
    sPending.push_back({b, entry});
    sQueueCv.notify_one();
}

static void fileCallback(s32 result, DVDCommandBlock* block) {
    DVDFileInfo* fi = (DVDFileInfo*)block;
    if (fi->callback) {
        fi->callback(result, fi);
    }
}

extern "C" {

void DVDInit(void) {
    static bool inited = false;
    if (inited) {
        return;
    }
    inited = true;
    OSInitThreadQueue(&sSyncQueue);
    if (!dvd_load_fst()) {
        port_fatal("dvd: game data not found under %s (expected sys/fst.bin and files/)", sDataRoot.c_str());
    }
    port_irq_set_handler(PORT_IRQ_DVD, dvdIrq);
    PortHostAllocScope scope;
    std::thread(workerMain).detach();
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo) {
    if (entrynum < 0 || (u32)entrynum >= sFst.size() || sFst[entrynum].isDir) {
        return FALSE;
    }
    memset(fileInfo, 0, sizeof(*fileInfo));
    fileInfo->startAddr = (u32)entrynum;
    fileInfo->length = (u32)dvd_entry_size((u32)entrynum);
    fileInfo->callback = nullptr;
    fileInfo->cb.state = kStateEnd;
    return TRUE;
}

BOOL DVDOpen(const char* fileName, DVDFileInfo* fileInfo) {
    s32 entry = DVDConvertPathToEntrynum(fileName);
    if (entry < 0) {
        port_log("dvd: file not found: %s", fileName);
        return FALSE;
    }
    return DVDFastOpen(entry, fileInfo);
}

BOOL DVDClose(DVDFileInfo* fileInfo) {
    (void)fileInfo;
    return TRUE;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio) {
    (void)prio;
    fileInfo->callback = callback;
    DVDCommandBlock* b = &fileInfo->cb;
    b->addr = addr;
    b->length = (u32)length;
    b->offset = (u32)offset;
    b->callback = fileCallback;
    submit(b, fileInfo->startAddr);
    return TRUE;
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio) {
    (void)prio;
    DVDCommandBlock* b = &fileInfo->cb;
    fileInfo->callback = nullptr;
    b->addr = addr;
    b->length = (u32)length;
    b->offset = (u32)offset;
    b->callback = nullptr;
    submit(b, fileInfo->startAddr);
    BOOL en = OSDisableInterrupts();
    while (b->state == kStateBusy || b->state == kStateWaiting) {
        OSSleepThread(&sSyncQueue);
    }
    OSRestoreInterrupts(en);
    return b->state == kStateEnd ? (s32)b->transferredSize : -1;
}

BOOL DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lk(sQueueMutex);
        for (auto it = sPending.begin(); it != sPending.end(); ++it) {
            if (it->block == block) {
                sPending.erase(it);
                removed = true;
                break;
            }
        }
    }
    if (removed) {
        block->state = kStateCanceled;
    }
    if (callback) {
        callback(0, block);
    }
    return TRUE;
}

s32 DVDCancel(DVDCommandBlock* block) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lk(sQueueMutex);
        for (auto it = sPending.begin(); it != sPending.end(); ++it) {
            if (it->block == block) {
                sPending.erase(it);
                removed = true;
                break;
            }
        }
    }
    if (removed) {
        block->state = kStateCanceled;
        return 0;
    }
    BOOL en = OSDisableInterrupts();
    while (block->state == kStateBusy) {
        OSSleepThread(&sSyncQueue);
    }
    OSRestoreInterrupts(en);
    return 0;
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) { return block->state; }

s32 DVDGetDriveStatus(void) {
    std::lock_guard<std::mutex> lk(sQueueMutex);
    return (sPending.empty() && sActive == nullptr) ? 0 : 1;
}

BOOL DVDCheckDiskAsync(DVDCommandBlock* block, DVDCBCallback callback) {
    block->state = kStateEnd;
    if (callback) {
        callback(1, block);
    }
    return TRUE;
}

BOOL DVDOpenDir(const char* dirName, DVDDir* dir) {
    s32 entry = DVDConvertPathToEntrynum(dirName);
    if (entry < 0 || !sFst[entry].isDir) {
        return FALSE;
    }
    dir->entryNum = (u32)entry;
    dir->location = (u32)entry + 1;
    dir->next = sFst[entry].next;
    return TRUE;
}

BOOL DVDReadDir(DVDDir* dir, DVDDirEntry* dirent) {
    u32 loc = dir->location;
    if (loc <= dir->entryNum || loc >= dir->next) {
        return FALSE;
    }
    FstEntry& fe = sFst[loc];
    dirent->entryNum = loc;
    dirent->isDir = fe.isDir;
    dirent->name = (char*)fe.name.c_str();
    dir->location = fe.isDir ? fe.next : loc + 1;
    return TRUE;
}

BOOL DVDCloseDir(DVDDir* dir) {
    (void)dir;
    return TRUE;
}

BOOL DVDChangeDir(const char* dirName) {
    s32 entry = DVDConvertPathToEntrynum(dirName);
    if (entry < 0 || !sFst[entry].isDir) {
        return FALSE;
    }
    sCurrentDir = (u32)entry;
    return TRUE;
}

}  // extern "C"
