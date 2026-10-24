// libreserve.so: preloaded into the app process by wrap.sh.
//
// A zygote-forked app process already has ART's large object space and JIT
// cache at ~0x77000000-0x9F000000, right across MEM1, MEM2 and the libgame.so
// slot, so the launcher cannot reserve the game's address window there.
// wrap.sh makes Android exec the app process instead of forking it from the
// zygote, and this constructor runs before app_process starts ART, so ART
// lays out its heap around the window.  launcher.c then finds the window
// already reserved.
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
#endif

#define DRIVER_LOG(...) __android_log_print(ANDROID_LOG_INFO, "GalaxyDriver", __VA_ARGS__)
#define DRIVER_WARN(...) __android_log_print(ANDROID_LOG_WARN, "GalaxyDriver", __VA_ARGS__)

// Same window as launcher.c.
static const uintptr_t kWindowBase = 0x80000000u;
static const size_t kWindowSize = 0x60010000u;

__attribute__((constructor)) static void reserveGameWindow(void) {
    void* win = mmap((void*)kWindowBase, kWindowSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (win != (void*)kWindowBase) {
        __android_log_print(ANDROID_LOG_ERROR, "PetariVR", "libreserve: cannot reserve the game address window at %p (got %p, errno %d)",
                            (void*)kWindowBase, win, errno);
        if (win != MAP_FAILED) {
            munmap(win, kWindowSize);
        }
        return;
    }
    // Shows up as [anon:petari game window] in /proc/<pid>/maps.
    prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, kWindowBase, kWindowSize, "petari game window");
}

// ---------------------------------------------------------------------------
// Custom Graphics Driver Interceptor
//
// A driver binary is user-supplied code that runs inside the game process, so
// the marker is only trusted from the app's private directory and the payload
// must stay inside it.  /sdcard is deliberately not consulted: any app with
// storage access can write there, which would turn the interposer into an
// arbitrary-library loader.
// ---------------------------------------------------------------------------
static int readActiveMarker(const char* path, char* out, size_t maxLen) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_mode & (S_IWGRP | S_IWOTH)) != 0 || st.st_size <= 0 || (size_t)st.st_size >= maxLen) {
        close(fd);
        return 0;
    }
    ssize_t n = read(fd, out, maxLen - 1);
    close(fd);
    if (n <= 0) {
        return 0;
    }
    out[n] = '\0';
    char* eol = strchr(out, '\n');
    if (eol != NULL) {
        *eol = '\0';
    }
    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\r' || out[len - 1] == ' ')) {
        out[--len] = '\0';
    }
    return len > 0;
}

static int hasParentTraversal(const char* path) {
    const char* p = path;
    while ((p = strstr(p, "..")) != NULL) {
        int atBoundary = (p == path || p[-1] == '/');
        int endsHere = (p[2] == '\0' || p[2] == '/');
        if (atBoundary && endsHere) {
            return 1;
        }
        p += 2;
    }
    return 0;
}

static int hasElfMagic(int fd) {
    unsigned char magic[4];
    return pread(fd, magic, sizeof(magic), 0) == (ssize_t)sizeof(magic) && magic[0] == 0x7f && magic[1] == 'E' &&
           magic[2] == 'L' && magic[3] == 'F';
}

static int validateDriverPath(const char* markerPath, const char* rawPath, char* outPath, size_t maxLen) {
    size_t rawLen = strlen(rawPath);
    if (rawPath[0] != '/' || rawLen < 8 || rawLen >= maxLen) {
        return 0;
    }
    if (strcmp(rawPath + rawLen - 3, ".so") != 0 || hasParentTraversal(rawPath)) {
        return 0;
    }

    char driverDir[PATH_MAX];
    snprintf(driverDir, sizeof(driverDir), "%s", markerPath);
    char* slash = strrchr(driverDir, '/');
    if (slash == NULL) {
        return 0;
    }
    *slash = '\0';

    char dirReal[PATH_MAX];
    char pathReal[PATH_MAX];
    if (realpath(driverDir, dirReal) == NULL || realpath(rawPath, pathReal) == NULL) {
        return 0;
    }
    size_t dirLen = strlen(dirReal);
    if (strncmp(pathReal, dirReal, dirLen) != 0 || pathReal[dirLen] != '/') {
        return 0;
    }

    struct stat st;
    if (lstat(pathReal, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_mode & (S_IWGRP | S_IWOTH)) != 0 || st.st_size <= 0 || st.st_size > 64 * 1024 * 1024) {
        return 0;
    }

    int fd = open(pathReal, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }
    int valid = hasElfMagic(fd);
    close(fd);
    if (!valid) {
        return 0;
    }

    snprintf(outPath, maxLen, "%s", pathReal);
    return 1;
}

static int getActiveCustomDriverPath(char* outPath, size_t maxLen) {
    const char* markers[] = {
        "/data/user/0/com.openport.galaxyandroid/files/custom_driver/active_driver.txt",
        "/data/data/com.openport.galaxyandroid/files/custom_driver/active_driver.txt",
    };

    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
        char rawPath[512];
        if (!readActiveMarker(markers[i], rawPath, sizeof(rawPath))) {
            continue;
        }
        if (validateDriverPath(markers[i], rawPath, outPath, maxLen)) {
            return 1;
        }
        DRIVER_WARN("Ignoring custom driver marker %s: %s is not a trusted driver in the app-private directory",
                    markers[i], rawPath);
    }
    return 0;
}

// Only HAL drivers are redirected.  Matching "vulkan." anywhere also matched
// the loader itself ("libvulkan.so"), which would replace the loader with an
// ICD.
static int isVulkanHalLibrary(const char* filename) {
    if (filename == NULL) {
        return 0;
    }
    const char* base = strrchr(filename, '/');
    base = base != NULL ? base + 1 : filename;
    size_t len = strlen(base);
    return len > 10 && strncmp(base, "vulkan.", 7) == 0 && strcmp(base + len - 3, ".so") == 0;
}

typedef void* (*DlopenExtFn)(const char*, int, const void*);
typedef void* (*DlopenFn)(const char*, int);

static DlopenExtFn sRealAndroidDlopenExt = NULL;
static DlopenFn sRealAndroidLoadSphal = NULL;

__attribute__((visibility("default"))) void* android_dlopen_ext(const char* filename, int flags, const void* extinfo) {
    if (!sRealAndroidDlopenExt) {
        sRealAndroidDlopenExt = (DlopenExtFn)dlsym(RTLD_NEXT, "android_dlopen_ext");
    }

    if (isVulkanHalLibrary(filename)) {
        char customDriverPath[PATH_MAX];
        if (getActiveCustomDriverPath(customDriverPath, sizeof(customDriverPath))) {
            DRIVER_LOG("Intercepting Vulkan HAL dlopen: %s -> redirecting to custom driver: %s", filename, customDriverPath);
            void* customHandle = sRealAndroidDlopenExt ? sRealAndroidDlopenExt(customDriverPath, flags, NULL)
                                                      : dlopen(customDriverPath, flags);
            if (customHandle) {
                DRIVER_LOG("Custom Vulkan driver loaded successfully into process");
                return customHandle;
            }
            DRIVER_WARN("Failed to load custom driver (%s): %s; falling back to system driver %s",
                        customDriverPath, dlerror(), filename);
        }
    }

    if (sRealAndroidDlopenExt) {
        return sRealAndroidDlopenExt(filename, flags, extinfo);
    }
    return dlopen(filename, flags);
}

__attribute__((visibility("default"))) void* android_load_sphal_library(const char* filename, int flags) {
    if (!sRealAndroidLoadSphal) {
        sRealAndroidLoadSphal = (DlopenFn)dlsym(RTLD_NEXT, "android_load_sphal_library");
    }

    if (isVulkanHalLibrary(filename)) {
        char customDriverPath[PATH_MAX];
        if (getActiveCustomDriverPath(customDriverPath, sizeof(customDriverPath))) {
            DRIVER_LOG("Intercepting sphal Vulkan load: %s -> redirecting to custom driver: %s", filename, customDriverPath);
            void* customHandle = sRealAndroidLoadSphal ? sRealAndroidLoadSphal(customDriverPath, flags)
                                                       : dlopen(customDriverPath, flags);
            if (customHandle) {
                DRIVER_LOG("Custom Vulkan driver loaded successfully via sphal hook");
                return customHandle;
            }
            DRIVER_WARN("Failed to load custom driver (%s) via sphal: %s; falling back to system driver",
                        customDriverPath, dlerror());
        }
    }

    if (sRealAndroidLoadSphal) {
        return sRealAndroidLoadSphal(filename, flags);
    }
    return dlopen(filename, flags);
}
