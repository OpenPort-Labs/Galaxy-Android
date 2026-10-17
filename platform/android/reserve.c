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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

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
// ---------------------------------------------------------------------------
static int getActiveCustomDriverPath(char* outPath, size_t maxLen) {
    const char* candidates[] = {
        "/data/user/0/com.openport.galaxyandroid/files/custom_driver/active_driver.txt",
        "/data/data/com.openport.galaxyandroid/files/custom_driver/active_driver.txt",
        "/sdcard/GalaxyData/custom_driver/active_driver.txt"
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        FILE* f = fopen(candidates[i], "r");
        if (f) {
            char line[512];
            if (fgets(line, sizeof(line), f)) {
                size_t len = strlen(line);
                while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
                    line[len - 1] = '\0';
                    len--;
                }
                fclose(f);
                if (len > 0 && access(line, R_OK) == 0) {
                    snprintf(outPath, maxLen, "%s", line);
                    return 1;
                }
            } else {
                fclose(f);
            }
        }
    }
    return 0;
}

typedef void* (*DlopenExtFn)(const char*, int, const void*);
typedef void* (*DlopenFn)(const char*, int);

static DlopenExtFn sRealAndroidDlopenExt = NULL;
static DlopenFn sRealAndroidLoadSphal = NULL;

__attribute__((visibility("default"))) void* android_dlopen_ext(const char* filename, int flags, const void* extinfo) {
    if (!sRealAndroidDlopenExt) {
        sRealAndroidDlopenExt = (DlopenExtFn)dlsym(RTLD_NEXT, "android_dlopen_ext");
    }

    if (filename && strstr(filename, "vulkan.")) {
        char customDriverPath[512];
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

    if (filename && strstr(filename, "vulkan.")) {
        char customDriverPath[512];
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
