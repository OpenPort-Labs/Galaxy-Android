#include "dvd_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <pthread.h>

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
pthread_t sWorkerThread;
bool sWorkerStarted = false;
// Keep only a bounded number of game files open: the FST can name thousands of
// entries and Android's per-process fd limit is small.
constexpr size_t kMaxOpenFds = 32;
std::deque<u32> sOpenOrder;

int openEntry(u32 entry) {
    if (entry >= sFst.size() || sFst[entry].isDir) {
        port_log("dvd: refusing to open entry %u (catalogue holds %zu)", entry, sFst.size());
        return -1;
    }
    FstEntry& fe = sFst[entry];
    if (fe.fd < 0) {
        while (sOpenOrder.size() >= kMaxOpenFds) {
            u32 victim = sOpenOrder.front();
            sOpenOrder.pop_front();
            if (victim < sFst.size() && sFst[victim].fd >= 0) {
                close(sFst[victim].fd);
                sFst[victim].fd = -1;
            }
        }
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
        if (fe.fd >= 0) {
            sOpenOrder.push_back(entry);
        }
    } else {
        auto it = std::find(sOpenOrder.begin(), sOpenOrder.end(), entry);
        if (it != sOpenOrder.end()) sOpenOrder.erase(it);
        sOpenOrder.push_back(entry);
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

void dvd_io_forget_fds() {
    std::lock_guard<std::mutex> lk(sQueueMutex);
    sOpenOrder.clear();
    for (auto& fe : sFst) {
        if (fe.fd >= 0) {
            close(fe.fd);
            fe.fd = -1;
        }
    }
}

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
    if (sWorkerRunning) return;
    sWorkerRunning = true;
    PortHostAllocScope scope;
    int rc = pthread_create(&sWorkerThread, nullptr, [](void*) -> void* { workerMain(); return nullptr; }, nullptr);
    if (rc != 0) {
        // Leaving sWorkerRunning set would make every later submit wait for
        // completions that can never arrive.
        sWorkerRunning = false;
        port_log("dvd: cannot start I/O worker: pthread_create failed (%d)", rc);
    } else {
        sWorkerStarted = true;
    }
}

void dvd_io_stop() {
    pthread_t worker;
    bool joinWorker = false;
    {
        std::lock_guard<std::mutex> lk(sQueueMutex);
        sWorkerRunning = false;
        sQueueCv.notify_all();
        if (sWorkerStarted) {
            worker = sWorkerThread;
            sWorkerStarted = false;
            joinWorker = true;
        }
    }
    if (joinWorker) {
        pthread_join(worker, nullptr);
    }
    dvd_io_forget_fds();
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
