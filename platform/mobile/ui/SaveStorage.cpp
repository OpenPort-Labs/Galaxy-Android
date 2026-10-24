#include "SaveStorage.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

#ifdef _WIN32
// Host-test portability; Android uses the POSIX calls directly.
#include <direct.h>
#include <io.h>
#define fsync _commit
#define lstat stat
#define mkdir(path, mode) _mkdir(path)
#endif

namespace mobile_ui {
namespace savefs {

namespace {

bool sameContents(const std::string& a, const std::string& b) {
    FILE* fa = fopen(a.c_str(), "rb");
    FILE* fb = fopen(b.c_str(), "rb");
    bool equal = fa && fb;
    char ba[16384], bb[16384];
    while (equal) {
        size_t na = fread(ba, 1, sizeof(ba), fa);
        size_t nb = fread(bb, 1, sizeof(bb), fb);
        if (na != nb || memcmp(ba, bb, na) != 0) equal = false;
        else if (na == 0) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return equal;
}

}  // namespace

bool removeDirRecursive(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return errno == ENOENT;
    if (!S_ISDIR(st.st_mode)) return unlink(path.c_str()) == 0;

    DIR* dir = opendir(path.c_str());
    if (!dir) return false;
    bool ok = true;
    while (struct dirent* entry = readdir(dir)) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (!removeDirRecursive(path + "/" + entry->d_name)) ok = false;
    }
    closedir(dir);
    if (rmdir(path.c_str()) != 0) ok = false;
    return ok;
}

bool copyFileVerified(const std::string& src, const std::string& dst) {
    struct stat st;
    if (stat(src.c_str(), &st) != 0 || st.st_size <= 0) return false;

    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;

    std::string tmpDst = dst + ".tmp";
    FILE* out = fopen(tmpDst.c_str(), "wb");
    if (!out) {
        fclose(in);
        return false;
    }

    char buf[16384];
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

    if (writeErr || total != (size_t)st.st_size || !sameContents(src, tmpDst)) {
        unlink(tmpDst.c_str());
        return false;
    }

    if (rename(tmpDst.c_str(), dst.c_str()) != 0) {
        unlink(tmpDst.c_str());
        return false;
    }
    return true;
}

bool copyDirVerified(const std::string& src, const std::string& dst) {
    if (mkdir(dst.c_str(), 0770) != 0 && errno != EEXIST) return false;
    DIR* dir = opendir(src.c_str());
    if (!dir) return false;
    struct dirent* entry;
    bool success = true;
    while ((entry = readdir(dir)) != nullptr) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (strstr(entry->d_name, ".tmp") || strstr(entry->d_name, "_rollback") || strstr(entry->d_name, "_staging")) continue;

        std::string sPath = src + "/" + entry->d_name;
        std::string dPath = dst + "/" + entry->d_name;
        struct stat st;
        if (stat(sPath.c_str(), &st) != 0) continue;
        bool copied;
        if (S_ISDIR(st.st_mode)) {
            copied = copyDirVerified(sPath, dPath);
        } else {
            copied = S_ISREG(st.st_mode) && copyFileVerified(sPath, dPath);
        }
        if (!copied) {
            success = false;
            break;
        }
    }
    closedir(dir);
    return success;
}

bool writeFileAtomic(const std::string& path, const void* data, size_t size) {
    if (path.empty() || !data || size == 0) return false;
    std::string tmpDst = path + ".tmp";
    int fd = open(tmpDst.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0660);
    if (fd < 0) return false;

    const char* bytes = static_cast<const char*>(data);
    size_t done = 0;
    bool ok = true;
    while (done < size) {
        ssize_t n = write(fd, bytes + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        done += static_cast<size_t>(n);
    }
    if (fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;

    if (!ok || rename(tmpDst.c_str(), path.c_str()) != 0) {
        unlink(tmpDst.c_str());
        return false;
    }
    // Best effort: persist the rename itself. Some Android FUSE mounts reject
    // directory fsync, and the file contents are already durable at this point.
    size_t slash = path.rfind('/');
    std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
    int dirFd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirFd >= 0) {
        fsync(dirFd);
        close(dirFd);
    }
    return true;
}

bool validateSaveDir(const std::string& dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;

    std::string directGameData = dir + "/GameData.bin";
    if (stat(directGameData.c_str(), &st) == 0 && st.st_size >= 512) return true;

    DIR* d = opendir(dir.c_str());
    if (!d) return false;
    struct dirent* entry;
    bool found = false;
    while ((entry = readdir(d)) != nullptr) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        std::string sub = dir + "/" + entry->d_name;
        if (stat(sub.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (validateSaveDir(sub)) {
                    found = true;
                    break;
                }
            } else if (strstr(entry->d_name, "GameData.bin") && st.st_size >= 512) {
                found = true;
                break;
            }
        }
    }
    closedir(d);
    return found;
}

}  // namespace savefs
}  // namespace mobile_ui
