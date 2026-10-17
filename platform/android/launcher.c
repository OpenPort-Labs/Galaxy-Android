// Tiny NativeActivity launcher.
//
// The game code assumes a 32-bit address space (it stores pointers in u32
// fields and uses Wii memory addresses directly).  Reserve 0x80000000-
// 0xE0010000 and load libgame.so into the 0x98000000 slot with
// android_dlopen_ext, then hand control to it.  In the app process the window
// is normally reserved already, by libreserve.so (see wrap.sh and reserve.c).
#include <android/dlext.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "PetariVR", __VA_ARGS__)
#define FATAL(...)                                                                                                                                   \
    do {                                                                                                                                             \
        __android_log_print(ANDROID_LOG_FATAL, "PetariVR", __VA_ARGS__);                                                                             \
        abort();                                                                                                                                     \
    } while (0)

static const uintptr_t kWindowBase = 0x80000000u;
static const size_t kWindowSize = 0x60010000u;  // up to 0xE0010000 (locked cache)
static const uintptr_t kLibBase = 0x98000000u;
static const size_t kLibSize = 0x08000000u;

typedef void (*PortAndroidMain)(struct android_app* app, uintptr_t windowBase, size_t windowSize);

// True when the whole window is one untouched PROT_NONE reservation (the one
// libreserve.so makes); otherwise logs the mappings that are in the way.
static int windowPreReserved(void) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) {
        return 0;
    }
    int reserved = 0;
    char line[4352];
    while (fgets(line, sizeof(line), f)) {
        unsigned long long lo, hi;
        char perms[8];
        if (sscanf(line, "%llx-%llx %7s", &lo, &hi, perms) != 3 || hi <= kWindowBase || lo >= kWindowBase + kWindowSize) {
            continue;
        }
        if (lo == kWindowBase && hi == kWindowBase + kWindowSize && !strcmp(perms, "---p")) {
            reserved = 1;
        } else {
            line[strcspn(line, "\n")] = 0;
            __android_log_print(ANDROID_LOG_ERROR, "PetariVR", "in the game address window: %s", line);
        }
    }
    fclose(f);
    return reserved;
}

void android_main(struct android_app* app) {
    void* win = mmap((void*)kWindowBase, kWindowSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (win != (void*)kWindowBase) {
        int err = errno;
        if (win != MAP_FAILED) {
            munmap(win, kWindowSize);  // placed elsewhere: the kernel ignored MAP_FIXED_NOREPLACE
        }
        if (!windowPreReserved()) {
            FATAL("cannot reserve the game address window at %p (errno %d), and libreserve.so did not reserve it either "
                  "(wrap.sh only runs for a debuggable APK with extracted native libs)",
                  (void*)kWindowBase, err);
        }
        LOG("game address window reserved by libreserve.so");
    }

    // libgame.so lives next to us, unless a developer build was pushed to the
    // app's private files directory (must be read-only for dlopen on Android 14).
    char path[512];
    const char* dataDir = app->activity->internalDataPath;
    snprintf(path, sizeof(path), "%s/libgame.so", dataDir ? dataDir : "");
    struct stat st;
    if (!dataDir || stat(path, &st) != 0) {
        Dl_info info;
        if (!dladdr((void*)android_main, &info) || !info.dli_fname) {
            FATAL("dladdr failed");
        }
        size_t used = (size_t)snprintf(path, sizeof(path), "%s", info.dli_fname);
        char* slash = strrchr(path, '/');
        if (!slash) {
            FATAL("bad library path %s", path);
        }
        if (used >= sizeof(path)) {
            // Truncated: appending the name would either overflow the buffer or
            // dlopen a path that is not the real library location.
            FATAL("library path too long for %zu bytes: %s", sizeof(path), path);
        }
        snprintf(slash + 1, sizeof(path) - (size_t)(slash + 1 - path), "libgame.so");
    } else {
        LOG("using developer libgame.so from %s", path);
    }

    android_dlextinfo ext;
    memset(&ext, 0, sizeof(ext));
    ext.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
    ext.reserved_addr = (void*)kLibBase;
    ext.reserved_size = kLibSize;
    void* lib = android_dlopen_ext(path, RTLD_NOW | RTLD_LOCAL, &ext);
    if (!lib) {
        FATAL("android_dlopen_ext(%s) failed: %s", path, dlerror());
    }
    PortAndroidMain entry = (PortAndroidMain)dlsym(lib, "port_android_main");
    if (!entry) {
        FATAL("port_android_main not found: %s", dlerror());
    }
    LOG("libgame.so loaded at %p", (void*)kLibBase);
    entry(app, kWindowBase, kWindowSize);
}
