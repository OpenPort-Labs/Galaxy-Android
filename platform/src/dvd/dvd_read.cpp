#include "dvd_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "port/heap_routing.h"
#include "port/port.h"
#include "revolution/dvd.h"

namespace {

struct ReadRequest {
    DVDCommandBlock* block;
    u32 entry;
};

std::mutex sQueueMutex;
std::condition_variable sQueueCv;
std::deque<ReadRequest> sPending;
std::deque<DVDCommandBlock*> sDone;
DVDCommandBlock* sActive = nullptr;
bool sActiveCanceled = false;
bool sWorkerRunning = false;

int openEntry(u32 entry) {
    if (entry >= sFst.size() || sFst[entry].isDir) {
        port_log("dvd: refusing to open entry %u (catalogue holds %zu)", entry, sFst.size());
        return -1;
    }
    FstEntry& fe = sFst[entry];
    if (fe.fd < 0) {
#if defined(_WIN32)
        DWORD attr = GetFileAttributesA(fe.hostPath.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
            port_log("dvd: refusing to open symlink %s", fe.hostPath.c_str());
            return -1;
        }
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
        fe.fd = open(fe.hostPath.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fe.fd < 0) {
            port_log("dvd: open %s failed: %s", fe.hostPath.c_str(), strerror(errno));
        }
    }
    return fe.fd;
}

void workerMain() {
    for (;;) {
        ReadRequest req;
        {
            std::unique_lock<std::mutex> lk(sQueueMutex);
            sQueueCv.wait(lk, [] { return !sPending.empty() || !sWorkerRunning; });
            if (!sWorkerRunning && sPending.empty()) {
                break;
            }
            req = sPending.front();
            sPending.pop_front();
            sActive = req.block;
            sActiveCanceled = false;
            sActive->state = kStateBusy;
        }
        DVDCommandBlock* b = req.block;
        s32 result;
        int fd = -1;
        {
            std::lock_guard<std::mutex> lk(sQueueMutex);
            fd = openEntry(req.entry);
        }
        if (fd < 0) {
            result = -1;
        } else {
            u8* dst = (u8*)b->addr;
            size_t want = b->length;
            s64 fileSize = dvd_entry_size(req.entry);
            size_t toRead = want;
            size_t pad = 0;
            if (fileSize > 0 && (s64)b->offset < fileSize && (s64)b->offset + (s64)want > fileSize) {
                toRead = (size_t)(fileSize - (s64)b->offset);
                pad = want - toRead;
            }
            ssize_t got = pread(fd, dst, toRead, (off_t)b->offset);
            if (got < 0 || (size_t)got != toRead) {
                port_log("dvd: short read on entry %u: wanted %zu, got %zd at %u", req.entry, want, got,
                         b->offset);
                result = -1;
            } else {
                if (pad > 0) {
                    memset(dst + toRead, 0, pad);
                }
                b->transferredSize = b->length;
                result = (s32)b->length;
            }
        }
        {
            std::lock_guard<std::mutex> lk(sQueueMutex);
            if (sActiveCanceled) {
                b->state = kStateCanceled;
                b->transferredSize = 0;
                b->currTransferSize = (u32)-3;
            } else {
                if (result < 0) {
                    b->transferredSize = 0;
                }
                b->currTransferSize = (u32)result;
            }
            sActive = nullptr;
            sActiveCanceled = false;
            sDone.push_back(b);
        }
        port_irq_raise(PORT_IRQ_DVD);
    }
}

}  // namespace

bool dvd_io_validate(const DVDFileInfo* fileInfo, const void* addr, s32 length, s32 offset, const char* api) {
    if (!fileInfo) {
        port_log("dvd: %s rejected null fileInfo", api);
        return false;
    }
    if (fileInfo->startAddr >= sFst.size() || sFst[fileInfo->startAddr].isDir) {
        port_log("dvd: %s rejected invalid/dir entry %u", api, fileInfo->startAddr);
        return false;
    }
    if (length < 0 || offset < 0) {
        port_log("dvd: %s rejected negative length %d / offset %d on entry %u", api, length, offset,
                 fileInfo->startAddr);
        return false;
    }
    if (length > 0 && addr == nullptr) {
        port_log("dvd: %s rejected null buffer for %d bytes", api, length);
        return false;
    }
    const s64 size = dvd_entry_size(fileInfo->startAddr);
    const s64 alignedSize = (size >= 0) ? ((size + 31) & ~31LL) : -1;
    if (size < 0 || (length > 0 && (s64)offset >= size) || (s64)offset + (s64)length > alignedSize) {
        port_log("dvd: %s rejected range [%d,%lld) of %lld-byte entry %u", api, offset,
                 (long long)offset + (long long)length, (long long)size, fileInfo->startAddr);
        return false;
    }
    return true;
}

void dvd_io_start() {
    std::lock_guard<std::mutex> lk(sQueueMutex);
    if (!sWorkerRunning) {
        sWorkerRunning = true;
        PortHostAllocScope scope;
        std::thread(workerMain).detach();
    }
}

void dvd_io_stop() {
    {
        std::lock_guard<std::mutex> lk(sQueueMutex);
        sWorkerRunning = false;
        sQueueCv.notify_all();
    }
}

BOOL dvd_io_submit(DVDCommandBlock* block, u32 entry) {
    if (!block) {
        return FALSE;
    }
    std::lock_guard<std::mutex> lk(sQueueMutex);
    if (block->state == kStateBusy || block->state == kStateWaiting) {
        port_log("dvd: refusing re-submission of busy/waiting command block %p", block);
        return FALSE;
    }
    block->state = kStateWaiting;
    block->transferredSize = 0;
    block->currTransferSize = 0;
    sPending.push_back({block, entry});
    sQueueCv.notify_one();
    return TRUE;
}

int dvd_io_cancel(DVDCommandBlock* block) {
    if (!block) {
        return 0;
    }
    std::lock_guard<std::mutex> lk(sQueueMutex);
    for (auto it = sPending.begin(); it != sPending.end(); ++it) {
        if (it->block == block) {
            sPending.erase(it);
            return 1;
        }
    }
    if (sActive == block) {
        sActiveCanceled = true;
        return 2;
    }
    return 0;
}

bool dvd_io_pop_done(DVDCommandBlock** block) {
    std::lock_guard<std::mutex> lk(sQueueMutex);
    if (sDone.empty()) {
        return false;
    }
    *block = sDone.front();
    sDone.pop_front();
    return true;
}

bool dvd_io_idle() {
    std::lock_guard<std::mutex> lk(sQueueMutex);
    return sPending.empty() && sActive == nullptr;
}
