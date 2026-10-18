// NAND flash emulation: save data lives in files under the app's storage.
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "port/heap_routing.h"
#include "port/port.h"
#include "revolution/nand.h"

static std::string sNandRoot;
static const char kHomeDir[] = "/title/00010000/524d4750/data";

extern "C" void port_nand_set_root(const char* root) { sNandRoot = root; }

static std::string hostPath(const char* path) {
    PortHostAllocScope scope;
    std::string p = path ? path : "";
    if (p.empty() || p[0] != '/') {
        p = std::string(kHomeDir) + "/" + p;
    }
    return sNandRoot + p;
}

static void makeDirs(const std::string& path) {
    PortHostAllocScope scope;
    for (size_t i = 1; i < path.size(); i++) {
        if (path[i] == '/') {
            std::string d = path.substr(0, i);
            mkdir(d.c_str(), 0770);
        }
    }
}

static s32 errnoToNand(int e) {
    switch (e) {
    case ENOENT:
        return NAND_RESULT_NOEXISTS;
    case EEXIST:
        return NAND_RESULT_EXISTS;
    case EACCES:
    case EPERM:
        return NAND_RESULT_ACCESS;
    case ENOSPC:
        return NAND_RESULT_MAXBLOCKS;
    case ENOTEMPTY:
        return NAND_RESULT_NOTEMPTY;
    default:
        return NAND_RESULT_UNKNOWN;
    }
}

extern "C" {

s32 NANDInit(void) { return NAND_RESULT_OK; }

s32 NANDGetHomeDir(char path[NAND_MAX_PATH]) {
    strcpy(path, kHomeDir);
    return NAND_RESULT_OK;
}

s32 NANDCreate(const char* path, u8 perm, u8 attr) {
    (void)perm;
    (void)attr;
    std::string hp = hostPath(path);
    makeDirs(hp);
    int fd = open(hp.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0660);
    if (fd < 0) {
        return errnoToNand(errno);
    }
    close(fd);
    return NAND_RESULT_OK;
}

s32 NANDPrivateCreate(const char* path, u8 perm, u8 attr) { return NANDCreate(path, perm, attr); }

s32 NANDOpen(const char* path, NANDFileInfo* info, u8 mode) {
    std::string hp = hostPath(path);
    int flags = O_CLOEXEC;
    switch (mode) {
    case NAND_ACCESS_READ:
        flags |= O_RDONLY;
        break;
    case NAND_ACCESS_WRITE:
        flags |= O_WRONLY;
        break;
    default:
        flags |= O_RDWR;
        break;
    }
    int fd = open(hp.c_str(), flags);
    if (fd < 0) {
        return errnoToNand(errno);
    }
    memset(info, 0, sizeof(*info));
    info->fileDescriptor = fd;
    info->accType = mode;
    strncpy(info->origPath, path, NAND_MAX_PATH - 1);
    return NAND_RESULT_OK;
}

s32 NANDPrivateOpen(const char* path, NANDFileInfo* info, u8 mode) { return NANDOpen(path, info, mode); }

static bool atomicMirrorFile(const std::string& src, const std::string& dst) {
    struct stat st;
    if (stat(src.c_str(), &st) != 0 || st.st_size < 512) {
        port_log("nand: atomicMirrorFile source invalid or too small: %s (size %ld)", src.c_str(), (long)(st.st_size));
        return false;
    }
    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;

    std::string tmpDst = dst + ".tmp";
    FILE* out = fopen(tmpDst.c_str(), "wb");
    if (!out) {
        fclose(in);
        return false;
    }

    char buf[8192];
    size_t n;
    size_t total = 0;
    bool writeErr = false;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            writeErr = true;
            break;
        }
        total += n;
    }
    fflush(out);
    int fd = fileno(out);
    if (fd >= 0) fsync(fd);
    fclose(out);
    fclose(in);

    if (writeErr || total != (size_t)st.st_size) {
        port_log("nand: atomicMirrorFile write failed (wrote %zu of %ld bytes)", total, (long)st.st_size);
        unlink(tmpDst.c_str());
        return false;
    }

    if (rename(tmpDst.c_str(), dst.c_str()) != 0) {
        unlink(tmpDst.c_str());
        return false;
    }
    return true;
}

s32 NANDClose(NANDFileInfo* info) {
    if (info->fileDescriptor >= 0) {
        if (info->accType != NAND_ACCESS_READ) {
            fsync(info->fileDescriptor);
            // Auto-mirror GameData.bin to persistent external storage (/sdcard/GalaxyData/saves) atomically
            if (strstr(info->origPath, "GameData.bin")) {
                std::string hp = hostPath(info->origPath);
                mkdir("/sdcard/GalaxyData", 0770);
                mkdir("/sdcard/GalaxyData/saves", 0770);
                if (atomicMirrorFile(hp, "/sdcard/GalaxyData/saves/GameData.bin")) {
                    port_log("nand: atomically mirrored %s -> /sdcard/GalaxyData/saves/GameData.bin", hp.c_str());
                } else {
                    port_log("nand: warning - atomic save mirror failed for %s", hp.c_str());
                }
            }
        }
        close(info->fileDescriptor);
        info->fileDescriptor = -1;
    }
    return NAND_RESULT_OK;
}

s32 NANDRead(NANDFileInfo* info, void* buf, u32 length) {
    ssize_t n = read(info->fileDescriptor, buf, length);
    return n < 0 ? errnoToNand(errno) : (s32)n;
}

s32 NANDWrite(NANDFileInfo* info, const void* buf, u32 length) {
    ssize_t n = write(info->fileDescriptor, buf, length);
    return n < 0 ? errnoToNand(errno) : (s32)n;
}

s32 NANDSeek(NANDFileInfo* info, s32 offset, s32 whence) {
    off_t r = lseek(info->fileDescriptor, offset, whence);
    return r < 0 ? errnoToNand(errno) : (s32)r;
}

s32 NANDGetLength(NANDFileInfo* info, u32* length) {
    struct stat st;
    if (fstat(info->fileDescriptor, &st) != 0) {
        return errnoToNand(errno);
    }
    *length = (u32)st.st_size;
    return NAND_RESULT_OK;
}

static int removeTree(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) {
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path.c_str());
        if (d) {
            while (struct dirent* e = readdir(d)) {
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                    continue;
                }
                removeTree(path + "/" + e->d_name);
            }
            closedir(d);
        }
        return rmdir(path.c_str());
    }
    return unlink(path.c_str());
}

s32 NANDDelete(const char* path) {
    PortHostAllocScope scope;
    std::string hp = hostPath(path);
    if (removeTree(hp) != 0) {
        return errnoToNand(errno);
    }
    return NAND_RESULT_OK;
}

s32 NANDMove(const char* path, const char* destDir) {
    PortHostAllocScope scope;
    std::string src = hostPath(path);
    std::string name = src.substr(src.rfind('/') + 1);
    std::string dst = hostPath(destDir) + "/" + name;
    makeDirs(dst);
    if (rename(src.c_str(), dst.c_str()) != 0) {
        return errnoToNand(errno);
    }
    return NAND_RESULT_OK;
}

s32 NANDCheck(u32 fsBlock, u32 inode, u32* answer) {
    (void)fsBlock;
    (void)inode;
    *answer = 0;  // enough space and inodes
    return NAND_RESULT_OK;
}

void NANDInitBanner(NANDBanner* banner, u32 flag, const u16* title, const u16* comment) {
    memset(banner, 0, sizeof(*banner));
    banner->signature = NAND_BANNER_SIGNATURE;
    banner->flag = flag;
    for (int i = 0; title && i < NAND_BANNER_COMMENT_SIZE && title[i]; i++) {
        banner->comment[0][i] = title[i];
    }
    for (int i = 0; comment && i < NAND_BANNER_COMMENT_SIZE && comment[i]; i++) {
        banner->comment[1][i] = comment[i];
    }
}

}  // extern "C"
