#include "path_security.h"
#include <cstring>
#include <algorithm>

#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#define MKDIR_SAFE(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#define MKDIR_SAFE(p) mkdir(p, 0755)
#endif

namespace wii {

bool isSafeName(const char* name) {
    if (!name || !*name) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    size_t len = 0;
    for (const char* p = name; *p; ++p, ++len) {
        unsigned char c = (unsigned char)*p;
        if (c == '/' || c == '\\' || c == ':' || c < 0x20 || c == 0x7F) {
            return false;
        }
    }
    if (len > 255) return false;
    if (name[len - 1] == ' ' || name[len - 1] == '.') return false;
    return true;
}

bool isReparseOrSymlink(const std::string& path) {
#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    return (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    struct stat st;
    if (lstat(path.c_str(), &st) == 0) {
        return S_ISLNK(st.st_mode);
    }
    return false;
#endif
}

bool isRealDirectory(const std::string& path) {
#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return false;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return false;
    if (S_ISLNK(st.st_mode)) return false;
    return S_ISDIR(st.st_mode);
#endif
}

std::string normalizePath(const std::string& path) {
    std::string res;
    res.reserve(path.size());
    for (char c : path) {
        res.push_back((c == '\\') ? '/' : c);
    }
    while (res.size() > 1 && res.back() == '/') {
        res.pop_back();
    }
    return res;
}

bool ensureSafeDirectories(const std::string& rootDir, const std::string& relDir) {
    if (rootDir.empty()) return false;
    std::string normRoot = normalizePath(rootDir);
    std::string normRel = normalizePath(relDir);

    if (!isRealDirectory(normRoot)) {
        if (isReparseOrSymlink(normRoot)) return false;
        MKDIR_SAFE(normRoot.c_str());
        if (!isRealDirectory(normRoot)) return false;
    }
    if (normRel.empty() || normRel == ".") return true;

    std::string current = normRoot;
    size_t start = 0;
    while (start < normRel.size()) {
        size_t slash = normRel.find('/', start);
        std::string comp = (slash == std::string::npos) ? normRel.substr(start)
                                                        : normRel.substr(start, slash - start);
        start = (slash == std::string::npos) ? normRel.size() : slash + 1;
        if (comp.empty()) continue;
        if (!isSafeName(comp.c_str())) return false;

        current += "/" + comp;
        if (isReparseOrSymlink(current)) return false;

        if (!isRealDirectory(current)) {
            if (MKDIR_SAFE(current.c_str()) != 0 && !isRealDirectory(current)) {
                return false;
            }
            if (isReparseOrSymlink(current)) {
                remove(current.c_str());
                return false;
            }
        }
    }
    return true;
}

FILE* openSafeFileForWrite(const std::string& rootDir, const std::string& relPath,
                           std::string* outFullPath) {
    if (rootDir.empty() || relPath.empty()) return nullptr;
    std::string normRoot = normalizePath(rootDir);
    std::string normRel = normalizePath(relPath);

    size_t lastSlash = normRel.find_last_of('/');
    std::string dirPart = (lastSlash == std::string::npos) ? "" : normRel.substr(0, lastSlash);
    std::string filePart = (lastSlash == std::string::npos) ? normRel : normRel.substr(lastSlash + 1);

    if (!isSafeName(filePart.c_str())) return nullptr;

    if (!dirPart.empty()) {
        if (!ensureSafeDirectories(normRoot, dirPart)) return nullptr;
    } else {
        if (!isRealDirectory(normRoot) && !ensureSafeDirectories(normRoot, "")) return nullptr;
    }

    std::string fullPath = normRoot + "/" + normRel;
    if (outFullPath) *outFullPath = fullPath;

    if (isReparseOrSymlink(fullPath) || isRealDirectory(fullPath)) {
        return nullptr;
    }

#if defined(_WIN32)
    FILE* fp = fopen(fullPath.c_str(), "wb");
    if (!fp) return nullptr;
    if (isReparseOrSymlink(fullPath)) {
        fclose(fp);
        remove(fullPath.c_str());
        return nullptr;
    }
    return fp;
#else
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
    int fd = open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) return nullptr;
    FILE* fp = fdopen(fd, "wb");
    if (!fp) {
        close(fd);
        remove(fullPath.c_str());
        return nullptr;
    }
    return fp;
#endif
}

void removePartialFile(const std::string& fullPath) {
    if (!fullPath.empty() && !isReparseOrSymlink(fullPath)) {
        remove(fullPath.c_str());
    }
}

} // namespace wii
