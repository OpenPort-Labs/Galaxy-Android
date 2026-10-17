// DVD file system over the cooked game data directory.
//
// Entry numbers come from the disc's own fst.bin, so they match the original
// game exactly; file sizes come from the cooked (decompressed, byte-swapped)
// files on the host.  Reads run on a worker thread and complete through the
// DVD interrupt, like the real drive.
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "port/heap_routing.h"
#include "port/port.h"
#include "revolution/dvd.h"
#include "revolution/os.h"
#include "revolution/sc.h"

enum {
    kStateEnd = 0,
    kStateBusy = 1,
    kStateWaiting = 2,
    kStateCanceled = 10,
    kStateFatal = -1,
};

struct FstEntry {
    bool isDir;
    std::string name;
    u32 parent;  // dirs: parent entry
    u32 next;    // dirs: index after the last child
    std::string hostPath;
    s64 size = -1;
    int fd = -1;
};

static std::vector<FstEntry> sFst;
static std::string sDataRoot;
static u32 sCurrentDir = 0;

static u32 be32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

// The disc's file table (sys/fst.bin under `root`): 12-byte entries, the
// root's holding their count, then the names.  Empty when missing or cut.
static std::vector<u8> readFst(const std::string& root) {
    std::vector<u8> data;
    FILE* f = fopen((root + "/sys/fst.bin").c_str(), "rb");
    if (!f) {
        return data;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len >= 12) {
        data.resize((size_t)len);
        if (fread(data.data(), 1, data.size(), f) != data.size() || (uint64_t)be32(&data[8]) * 12 >= data.size()) {
            data.clear();
        }
    }
    fclose(f);
    return data;
}

// Whether the disc's root directory has a folder `name`.
static bool fstHasRootDir(const std::vector<u8>& fst, const char* name) {
    u32 count = be32(&fst[8]);
    const char* strings = (const char*)&fst[count * 12];
    size_t stringsSize = fst.size() - count * 12;
    size_t len = strlen(name);
    for (u32 i = 1; i < count;) {
        const u8* e = &fst[i * 12];
        bool isDir = e[0] != 0;
        u32 nameOff = be32(e) & 0x00FFFFFF;
        if (isDir && nameOff + len < stringsSize && strncasecmp(strings + nameOff, name, len + 1) == 0) {
            return true;
        }
        u32 next = be32(e + 8);
        i = (isDir && next > i) ? next : i + 1;  // past a folder's contents
    }
    return false;
}

// Each region's disc keeps its texts and translated layouts in language
// folders named after it (EuEnglish, UsEnglish...), and the game picks the
// folder from the disc's game code and the console's language
// (Language.cpp).  The port tells the disc by those folders in its file
// table, which every conversion has, and runs the game in the language of
// the `language` setting, or the disc's first where it has no such texts.
// (The European disc also has an EuDutch folder: its texts are the English
// ones.)
static const PortDisc kDiscs[] = {
    {"RMGP01",
     "Europe",
     {{"english", "EuEnglish", SC_LANG_ENGLISH},
      {"french", "EuFrench", SC_LANG_FRENCH},
      {"german", "EuGerman", SC_LANG_GERMAN},
      {"spanish", "EuSpanish", SC_LANG_SPANISH},
      {"italian", "EuItalian", SC_LANG_ITALIAN}},
     5},
    {"RMGE01",
     "North America",
     {{"english", "UsEnglish", SC_LANG_ENGLISH}, {"french", "UsFrench", SC_LANG_FRENCH}, {"spanish", "UsSpanish", SC_LANG_SPANISH}},
     3},
    // Not tried: no such disc at hand.
    {"RMGJ01", "Japan", {{"japanese", "JpJapanese", SC_LANG_JAPANESE}}, 1},
    {"RMGK01", "Korea", {{"korean", "KrKorean", SC_LANG_KOREAN}}, 1},
};
static const PortDisc* sDisc = nullptr;
// The running disc's languages whose texts are in the copy (the first is the
// disc's own, which port_dvd_identify checked), the one asked for
// (port_language_set) and the one the game started in.
static const PortLanguage* sLanguages[PORT_DISC_MAX_LANGUAGES];
static int sLanguageCount = 0;
static char sLanguageWanted[16];
static const PortLanguage* sLanguageRunning = nullptr;

// Whether the copy in `root` has the texts of a language of its disc.
static bool hasTexts(const std::string& root, const std::vector<u8>& fst, const PortLanguage& language) {
    std::string texts = root + "/files/" + language.folder + "/MessageData/Message.arc";
    struct stat st;
    return fstHasRootDir(fst, language.folder) && stat(texts.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

extern "C" const PortDisc* port_dvd_identify(const char* root) {
    PortHostAllocScope scope;
    std::vector<u8> fst = readFst(root);
    if (fst.empty()) {
        return nullptr;
    }
    for (const PortDisc& disc : kDiscs) {
        // The disc has the folder of its first language, and the copy the
        // texts in it.
        if (hasTexts(root, fst, disc.languages[0])) {
            return &disc;
        }
    }
    return nullptr;
}

extern "C" const PortDisc* port_dvd_disc(void) { return sDisc; }

extern "C" void port_dvd_set_root(const char* root) {
    PortHostAllocScope scope;
    sDataRoot = root;
    sDisc = port_dvd_identify(root);
    sLanguageCount = 0;
    if (sDisc) {
        std::vector<u8> fst = readFst(root);
        for (int i = 0; i < sDisc->languageCount; i++) {
            if (i == 0 || hasTexts(root, fst, sDisc->languages[i])) {
                sLanguages[sLanguageCount++] = &sDisc->languages[i];
            }
        }
    }
    // Debug: PETARI_LANGUAGE=<name> instead of the setting.
    if (const char* name = getenv("PETARI_LANGUAGE")) {
        port_language_set(name);
    }
}

extern "C" int port_language_count(void) { return sLanguageCount; }

extern "C" const char* port_language_name(int index) { return index >= 0 && index < sLanguageCount ? sLanguages[index]->name : ""; }

extern "C" void port_language_set(const char* name) {
    size_t n = 0;
    for (; name && name[n] && n + 1 < sizeof(sLanguageWanted); n++) {
        sLanguageWanted[n] = (char)tolower((unsigned char)name[n]);
    }
    sLanguageWanted[n] = 0;
}

extern "C" int port_language_wanted(void) {
    if (sLanguageWanted[0] == 0) {
        return 0;
    }
    for (int i = 0; i < sLanguageCount; i++) {
        if (strcmp(sLanguageWanted, sLanguages[i]->name) == 0) {
            return i;
        }
    }
    // Also match 2-letter ISO codes and prefixes
    for (int i = 0; i < sLanguageCount; i++) {
        const char* lang = sLanguages[i]->name;
        if (strncmp(sLanguageWanted, lang, strlen(sLanguageWanted)) == 0) return i;
        if ((strcmp(sLanguageWanted, "es") == 0 || strcmp(sLanguageWanted, "spa") == 0) && strcmp(lang, "spanish") == 0) return i;
        if ((strcmp(sLanguageWanted, "fr") == 0 || strcmp(sLanguageWanted, "fra") == 0) && strcmp(lang, "french") == 0) return i;
        if ((strcmp(sLanguageWanted, "de") == 0 || strcmp(lang, "ger") == 0) && strcmp(lang, "german") == 0) return i;
        if ((strcmp(sLanguageWanted, "it") == 0 || strcmp(sLanguageWanted, "ita") == 0) && strcmp(lang, "italian") == 0) return i;
        if ((strcmp(sLanguageWanted, "en") == 0 || strcmp(sLanguageWanted, "eng") == 0) && strcmp(lang, "english") == 0) return i;
        if ((strcmp(sLanguageWanted, "ja") == 0 || strcmp(sLanguageWanted, "jpn") == 0) && strcmp(lang, "japanese") == 0) return i;
        if ((strcmp(sLanguageWanted, "ko") == 0 || strcmp(sLanguageWanted, "kor") == 0) && strcmp(lang, "korean") == 0) return i;
    }
    return 0;
}

extern "C" int port_language_running(void) {
    for (int i = 0; i < sLanguageCount; i++) {
        if (sLanguages[i] == sLanguageRunning) {
            return i;
        }
    }
    return -1;
}

extern "C" int port_language_start(void) {
    if (sLanguageCount == 0) {
        return SC_LANG_ENGLISH;
    }
    const PortLanguage* language = sLanguages[port_language_wanted()];
    if (language != sLanguageRunning) {
        sLanguageRunning = language;
        port_log("language: %s (texts from %s)", language->name, language->folder);
    }
    return language->code;
}

extern "C" const char* port_dvd_root(void) { return sDataRoot.c_str(); }

static bool loadFst() {
    PortHostAllocScope scope;
    std::vector<u8> data = readFst(sDataRoot);
    if (data.empty()) {
        port_log("dvd: cannot read %s/sys/fst.bin", sDataRoot.c_str());
        return false;
    }

    u32 count = be32(&data[8]);
    // readFst() has already checked that the entry table fits in the file, but a
    // zero count would leave the root entry missing and the name offsets below
    // need a bounded string table to be validated against.
    if (count < 1) {
        port_log("dvd: fst holds no entries");
        return false;
    }
    const u8* strings = &data[(size_t)count * 12];
    const size_t stringsSize = data.size() - (size_t)count * 12;
    sFst.resize(count);
    // Directory path of each directory entry, built as we walk.
    std::vector<std::string> dirPath(count);
    dirPath[0] = "";
    std::vector<u32> stack;  // (dir index) while walking
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
        u32 nameOff = be32(e) & 0x00FFFFFF;
        // An offset outside the string table, or a name that never terminates
        // inside it, would make the std::string below scan past the buffer.
        if (nameOff >= stringsSize || !memchr(strings + nameOff, '\0', stringsSize - nameOff)) {
            port_log("dvd: fst entry %u has an out-of-range name offset", i);
            return false;
        }
        const char* name = (const char*)(strings + nameOff);
        // Names become host paths, so no separator, parent reference or empty name.
        if (*name == '\0' || strchr(name, '/') || strchr(name, '\\') || strcmp(name, "..") == 0) {
            port_log("dvd: fst entry %u has an unusable name", i);
            return false;
        }
        fe.name = name;
        u32 parentDir = stack.back();
        std::string full = dirPath[parentDir].empty() ? fe.name : dirPath[parentDir] + "/" + fe.name;
        if (fe.isDir) {
            u32 parent = be32(e + 4);
            u32 next = be32(e + 8);
            // findChild() walks entries up to `next` and `..` jumps to `parent`,
            // both by indexing sFst, so an unchecked span reads past the table.
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

static s64 entrySize(u32 entry) {
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

// Case-insensitive lookup of `name` among the children of directory `dir`.
static s32 findChild(u32 dir, const char* name, size_t len) {
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
            found = findChild(dir, p, len);
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

// ---------------------------------------------------------------------------
// Read worker
// ---------------------------------------------------------------------------
struct ReadRequest {
    DVDCommandBlock* block;
    u32 entry;
};

static std::mutex sQueueMutex;
static std::condition_variable sQueueCv;
static std::deque<ReadRequest> sPending;       // not yet started
static std::deque<DVDCommandBlock*> sDone;     // finished, callbacks pending
static DVDCommandBlock* sActive = nullptr;     // being read by the worker
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
                    memset(dst + got, 0, want - (size_t)got);  // past EOF the drive returns adjacent data
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

// DVDFileInfo embeds its command block first; the file callback gets the
// file info back (as on hardware).
static void fileCallback(s32 result, DVDCommandBlock* block) {
    DVDFileInfo* fi = (DVDFileInfo*)block;
    if (fi->callback) {
        fi->callback(result, fi);
    }
}

// ---------------------------------------------------------------------------
// SDK API
// ---------------------------------------------------------------------------
extern "C" {

void DVDInit(void) {
    static bool inited = false;
    if (inited) {
        return;
    }
    inited = true;
    OSInitThreadQueue(&sSyncQueue);
    if (!loadFst()) {
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
    fileInfo->length = (u32)entrySize((u32)entrynum);
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
    // In flight: wait for it to finish.
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
