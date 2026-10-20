#include "dvd_internal.h"

#include <string.h>

#include "port/port.h"
#include "revolution/dvd.h"
#include "revolution/os.h"

static OSThreadQueue sSyncQueue;

static void dvdIrq() {
    for (;;) {
        DVDCommandBlock* b;
        if (!dvd_io_pop_done(&b)) {
            break;
        }
        s32 result = (s32)b->currTransferSize;
        if (b->state != kStateCanceled) {
            b->state = result < 0 ? kStateFatal : kStateEnd;
        }
        if (b->callback) {
            b->callback(result, b);
        }
    }
    OSWakeupThread(&sSyncQueue);
}

static void fileCallback(s32 result, DVDCommandBlock* block) {
    DVDFileInfo* fi = (DVDFileInfo*)block;
    if (fi && fi->callback) {
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
    dvd_io_start();
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo) {
    if (!fileInfo || entrynum < 0 || (u32)entrynum >= sFst.size() || sFst[entrynum].isDir) {
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
    if (!fileName || !fileInfo) {
        return FALSE;
    }
    s32 entry = DVDConvertPathToEntrynum(fileName);
    if (entry < 0) {
        port_log("dvd: file not found: %s", fileName);
        return FALSE;
    }
    return DVDFastOpen(entry, fileInfo);
}

BOOL DVDClose(DVDFileInfo* fileInfo) {
    return fileInfo != nullptr ? TRUE : FALSE;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio) {
    (void)prio;
    if (!fileInfo) {
        return FALSE;
    }
    fileInfo->callback = callback;
    DVDCommandBlock* b = &fileInfo->cb;
    b->addr = addr;
    b->length = (u32)length;
    b->offset = (u32)offset;
    b->callback = fileCallback;
    if (!dvd_io_validate(fileInfo, addr, length, offset, "DVDReadAsyncPrio")) {
        b->state = kStateFatal;
        b->transferredSize = 0;
        b->currTransferSize = (u32)-1;
        if (callback) {
            callback(-1, fileInfo);
        }
        return FALSE;
    }
    if (!dvd_io_submit(b, fileInfo->startAddr)) {
        b->state = kStateFatal;
        b->transferredSize = 0;
        b->currTransferSize = (u32)-1;
        if (callback) {
            callback(-1, fileInfo);
        }
        return FALSE;
    }
    return TRUE;
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio) {
    (void)prio;
    if (!fileInfo) {
        return -1;
    }
    DVDCommandBlock* b = &fileInfo->cb;
    fileInfo->callback = nullptr;
    b->addr = addr;
    b->length = (u32)length;
    b->offset = (u32)offset;
    b->callback = nullptr;
    if (!dvd_io_validate(fileInfo, addr, length, offset, "DVDReadPrio")) {
        b->state = kStateFatal;
        b->transferredSize = 0;
        b->currTransferSize = (u32)-1;
        return -1;
    }
    if (!dvd_io_submit(b, fileInfo->startAddr)) {
        b->state = kStateFatal;
        b->transferredSize = 0;
        b->currTransferSize = (u32)-1;
        return -1;
    }
    BOOL en = OSDisableInterrupts();
    while (b->state == kStateBusy || b->state == kStateWaiting) {
        OSSleepThread(&sSyncQueue);
    }
    OSRestoreInterrupts(en);
    return b->state == kStateEnd ? (s32)b->transferredSize : -1;
}

BOOL DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback) {
    if (!block) {
        return FALSE;
    }
    int res = dvd_io_cancel(block);
    if (res == 1) {
        block->state = kStateCanceled;
        block->currTransferSize = (u32)-3;
        if (block->callback) {
            block->callback(-3, block);
        }
    }
    if (callback) {
        callback(0, block);
    }
    return TRUE;
}

s32 DVDCancel(DVDCommandBlock* block) {
    if (!block) {
        return -1;
    }
    int res = dvd_io_cancel(block);
    if (res == 1) {
        block->state = kStateCanceled;
        block->currTransferSize = (u32)-3;
        if (block->callback) {
            block->callback(-3, block);
        }
        return 0;
    }
    BOOL en = OSDisableInterrupts();
    while (block->state == kStateBusy || block->state == kStateWaiting) {
        OSSleepThread(&sSyncQueue);
    }
    OSRestoreInterrupts(en);
    return 0;
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) {
    return block ? block->state : kStateFatal;
}

s32 DVDGetDriveStatus(void) { return dvd_io_idle() ? 0 : 1; }

BOOL DVDCheckDiskAsync(DVDCommandBlock* block, DVDCBCallback callback) {
    if (!block) {
        return FALSE;
    }
    block->state = kStateEnd;
    if (callback) {
        callback(1, block);
    }
    return TRUE;
}

BOOL DVDOpenDir(const char* dirName, DVDDir* dir) {
    if (!dirName || !dir) {
        return FALSE;
    }
    s32 entry = DVDConvertPathToEntrynum(dirName);
    if (entry < 0 || (u32)entry >= sFst.size() || !sFst[entry].isDir) {
        return FALSE;
    }
    dir->entryNum = (u32)entry;
    dir->location = (u32)entry + 1;
    dir->next = sFst[entry].next;
    return TRUE;
}

BOOL DVDReadDir(DVDDir* dir, DVDDirEntry* dirent) {
    if (!dir || !dirent) {
        return FALSE;
    }
    u32 loc = dir->location;
    if (loc <= dir->entryNum || loc >= dir->next || loc >= sFst.size()) {
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
    return dir != nullptr ? TRUE : FALSE;
}

BOOL DVDChangeDir(const char* dirName) {
    if (!dirName) {
        return FALSE;
    }
    s32 entry = DVDConvertPathToEntrynum(dirName);
    if (entry < 0 || (u32)entry >= sFst.size() || !sFst[entry].isDir) {
        return FALSE;
    }
    sCurrentDir = (u32)entry;
    return TRUE;
}

}  // extern "C"
