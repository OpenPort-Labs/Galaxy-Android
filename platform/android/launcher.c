// Tiny NativeActivity launcher.
//
// The game code assumes a 32-bit address space (it stores pointers in u32
// fields and uses Wii memory addresses directly).  Reserve guest slices safely
// and load libgame.so into the 0x98000000 slot with android_dlopen_ext, then
// hand control to it.  In tester builds the window is reserved by libreserve.so
// (see wrap.sh and reserve.c).
#include <android/dlext.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "port/guest_layout.h"

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

void android_main(struct android_app* app) {
    GuestLayoutAudit audit;
    int reserved = guest_layout_reserve_slices_nondestructive(&audit);
    if (!reserved || !audit.fullyAvailable) {
        char report[1024];
        guest_layout_format_audit_report(&audit, report, sizeof(report));
        __android_log_print(ANDROID_LOG_WARN, "PetariVR",
            "cannot reserve all guest slices cleanly (%d conflicts detected);\n%s",
            audit.totalConflicts, report);
    } else {
        LOG("guest address slices verified and reserved safely");
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
        if (audit.regions[GUEST_REGION_LIB].status == GUEST_RES_CONFLICT) {
            FATAL("android_dlopen_ext(%s) failed at %p: %s (slot conflict: %s)",
                  path, (void*)kLibBase, dlerror(), audit.regions[GUEST_REGION_LIB].conflictOwner);
        }
        FATAL("android_dlopen_ext(%s) failed at %p: %s", path, (void*)kLibBase, dlerror());
    }
    PortAndroidMain entry = (PortAndroidMain)dlsym(lib, "port_android_main");
    if (!entry) {
        FATAL("port_android_main not found: %s", dlerror());
    }
    LOG("libgame.so loaded at %p", (void*)kLibBase);
    entry(app, kWindowBase, kWindowSize);
}
