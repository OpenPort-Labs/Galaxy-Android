// Bring-up tool: boots libgame.so from an adb shell, without VR or a window.
//
//   adb push build-android/petari_headless build-android/libgame.so /data/local/tmp/petari/
//   adb shell /data/local/tmp/petari/petari_headless <data root> <save root> [seconds]
//
// Reserves the game's low address window exactly like the NativeActivity
// launcher, loads libgame.so at 0x98000000, calls port_boot() and keeps the
// process alive while the emulated game threads run.  Logs are mirrored to
// stderr.
//
// Controller 0 is a connected Wii remote + Nunchuk at rest.  PETARI_INPUT
// scripts it (see port/input_script.h).
#include <android/dlext.h>
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "port/input.h"
#include "port/input_script.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

// Value of an environment variable, or NULL when unset or empty.
static const char* envSet(const char* name) {
    const char* v = getenv(name);
    return v && *v ? v : NULL;
}

// PETARI_WANDER: deterministic pseudo-random play for soak tests.  The stick
// picks a new direction every 1.5 s; some of those segments also jump, spin
// or crouch; now and then B, which shoots star bits or backs out of dialogs.
static void wander(PortPadState* pad, int ms) {
    unsigned seg = (unsigned)ms / 1500u, t = (unsigned)ms % 1500u;
    unsigned h = (seg + 1u) * 2654435761u;
    float ang = (float)(h % 360u) * 3.14159265f / 180.0f;
    pad->stickX = cosf(ang);
    pad->stickY = sinf(ang);
    if ((h >> 8) % 3u == 0 && t >= 300 && t < 450) pad->buttons |= 0x0800;  // A
    if ((h >> 12) % 4u == 0 && t >= 800 && t < 1000) {                       // spin
        float s = (t / 16u) & 1u ? 2.5f : -2.5f;
        pad->accX = s;
        pad->accY = s;
    }
    if ((h >> 16) % 9u == 0 && t >= 1100 && t < 1400) pad->buttons |= 0x2000;  // Z
    if ((h >> 20) % 7u == 0 && t >= 600 && t < 750) pad->buttons |= 0x0400;     // B: shoots, or backs out of dialogs
}

// Resident and peak memory of this process, from /proc.
static void logMemory(double seconds) {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return;
    char line[256];
    long rss = 0, hwm = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "VmRSS:", 6)) rss = atol(line + 6);
        if (!strncmp(line, "VmHWM:", 6)) hwm = atol(line + 6);
    }
    fclose(f);
    fprintf(stderr, "headless: %.0f s memory rss %ld MB peak %ld MB\n", seconds, rss / 1024, hwm / 1024);
}

// Controller state at `ms` since boot.
static void padAt(const PortInputEvent* ev, int count, int ms, PortPadState* pad) {
    memset(pad, 0, sizeof(*pad));
    pad->connected = 1;
    pad->pointerDist = 2.0f;
    portApplyInputScript(ev, count, ms, pad);
}

static long long nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static const uintptr_t kWindowBase = 0x80000000u;
static const size_t kWindowSize = 0x60010000u;
static const uintptr_t kLibBase = 0x98000000u;
static const size_t kLibSize = 0x08000000u;

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <data root> <save root> [seconds]\n", argv[0]);
        return 2;
    }
    int seconds = argc > 3 ? atoi(argv[3]) : 60;

    void* win = mmap((void*)kWindowBase, kWindowSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (win != (void*)kWindowBase) {
        fprintf(stderr, "cannot reserve %p: got %p errno %d\n", (void*)kWindowBase, win, errno);
        return 1;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s", argv[0]);
    char* slash = strrchr(path, '/');
    snprintf(slash ? slash + 1 : path, sizeof(path) - (slash ? (size_t)(slash + 1 - path) : 0), "libgame.so");

    android_dlextinfo ext;
    memset(&ext, 0, sizeof(ext));
    ext.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
    ext.reserved_addr = (void*)kLibBase;
    ext.reserved_size = kLibSize;
    void* lib = android_dlopen_ext(path, RTLD_NOW | RTLD_LOCAL, &ext);
    if (!lib) {
        fprintf(stderr, "dlopen %s: %s\n", path, dlerror());
        return 1;
    }

    int* toStderr = (int*)dlsym(lib, "port_log_to_stderr");
    if (toStderr) {
        *toStderr = 1;
    }
    void (*setWindow)(uintptr_t, size_t) = (void (*)(uintptr_t, size_t))dlsym(lib, "port_mem_set_reserved_window");
    void (*boot)(const char*, const char*) = (void (*)(const char*, const char*))dlsym(lib, "port_boot");
    if (!setWindow || !boot) {
        fprintf(stderr, "missing entry points: %s\n", dlerror());
        return 1;
    }
    setWindow(kWindowBase, kWindowSize);
    int* traceNerves = (int*)dlsym(lib, "port_trace_nerves");
    if (traceNerves && envSet("PETARI_NERVES")) {
        *traceNerves = atoi(envSet("PETARI_NERVES"));
    }
    void (*inputSet)(int, const PortPadState*) = (void (*)(int, const PortPadState*))dlsym(lib, "port_input_set");
    static PortInputEvent events[1024];
    int eventCount = envSet("PETARI_INPUT") ? portParseInputScript(envSet("PETARI_INPUT"), events, 1024) : 0;
    PortPadState pad;
    padAt(events, eventCount, 0, &pad);
    if (inputSet) inputSet(0, &pad);

    fprintf(stderr, "libgame.so loaded at %p; booting\n", (void*)kLibBase);
    long long start = nowMs();
    // The boot time, for debug hooks timed like PETARI_INPUT (PETARI_WARP).
    char startText[32];
    snprintf(startText, sizeof(startText), "%lld", start);
    setenv("PETARI_T0_MS", startText, 1);
    boot(argv[1], argv[2]);

    // Feed the scripted input every 5 ms and render a snapshot of the latest
    // game frame every PETARI_SHOT_MS (default 2 s).
    unsigned long long (*render)(const char*, int) = (unsigned long long (*)(const char*, int))dlsym(lib, "port_headless_render");
    char shot[512];
    int shotIndex = 0;
    int intervalMs = envSet("PETARI_SHOT_MS") ? atoi(envSet("PETARI_SHOT_MS")) : 2000;
    if (intervalMs < 100) intervalMs = 100;
    // PETARI_SHOT_FROM=<s>: the first snapshot at that time (bursts of close
    // snapshots late in a run).
    long long nextShot = start + (envSet("PETARI_SHOT_FROM") ? (long long)(atof(envSet("PETARI_SHOT_FROM")) * 1000.0) : intervalMs);
    long long nextMemory = start + 30000;
    int wanderFrom = envSet("PETARI_WANDER") ? atoi(envSet("PETARI_WANDER")) * 1000 : -1;
    // PETARI_XRSIM_FPS=<hz>: keep rendering the simulated headset view at
    // that rate between snapshots (profiling with PETARI_PERFLOG=1).
    double xrsimFps = envSet("PETARI_XRSIM_FPS") ? atof(envSet("PETARI_XRSIM_FPS")) : 0.0;
    long long nextXrsim = start;
    void (*xrsimFn)(const char*, int, float, float) = (void (*)(const char*, int, float, float))dlsym(lib, "port_headless_xrsim");
    uint32_t lastButtons = 0;
    for (;;) {
        long long now = nowMs();
        if (now - start >= (long long)seconds * 1000) break;
        padAt(events, eventCount, (int)(now - start), &pad);
        if (wanderFrom >= 0 && now - start >= wanderFrom) wander(&pad, (int)(now - start - wanderFrom));
        if (now >= nextMemory) {
            nextMemory += 30000;
            logMemory((now - start) / 1000.0);
        }
        if (inputSet) inputSet(0, &pad);
        if (pad.buttons != lastButtons) {
            fprintf(stderr, "headless: %.2f s buttons %04x\n", (now - start) / 1000.0, pad.buttons);
            lastButtons = pad.buttons;
        }
        if (xrsimFps > 0.0 && xrsimFn && shotIndex > 0 && now >= nextXrsim && now < nextShot) {  // after the first snapshot made the GL context; a due snapshot comes first
            nextXrsim += (long long)(1000.0 / xrsimFps);
            if (nextXrsim < now) nextXrsim = now;
            xrsimFn(NULL, 1, 0.0f, -20.0f);
            continue;
        }
        if (now < nextShot) {
            usleep(xrsimFps > 0.0 ? 1000 : 5000);
            continue;
        }
        nextShot += intervalMs;
        if (render) {
            snprintf(shot, sizeof(shot), "%s/../shots/frame_%03d.png", argv[2], shotIndex);
            render(shot, 2);
            if (envSet("PETARI_XRSIM")) {
                // The headset presentation (vr_game.cpp) with simulated eyes.
                void (*xrsim)(const char*, int, float, float) = (void (*)(const char*, int, float, float))dlsym(lib, "port_headless_xrsim");
                float yaw = 0, pitch = -20;
                if (envSet("PETARI_XRHEAD")) sscanf(envSet("PETARI_XRHEAD"), "%f,%f", &yaw, &pitch);
                snprintf(shot, sizeof(shot), "%s/../shots/frame_%03d_xr.png", argv[2], shotIndex);
                // PETARI_XRSIM_STEPS: display frames simulated per snapshot
                // (1 with PETARI_XRSIM_FPS for a continuous sequence).
                int steps = envSet("PETARI_XRSIM_STEPS") ? atoi(envSet("PETARI_XRSIM_STEPS")) : 40;
                if (xrsim) xrsim(shot, steps > 0 ? steps : 1, yaw, pitch);
            }
            if (envSet("PETARI_DUMP")) {
                void (*dump)(const char*) = (void (*)(const char*))dlsym(lib, "port_headless_dump");
                snprintf(shot, sizeof(shot), "%s/../shots/frame_%03d", argv[2], shotIndex);
                if (dump) dump(shot);
            }
            shotIndex++;
        }
    }
    if (envSet("PETARI_THREADS")) {
        void (*dumpThreads)(void) = (void (*)(void))dlsym(lib, "port_debug_dump_threads");
        if (dumpThreads) dumpThreads();
    }
    fprintf(stderr, "headless: %d s elapsed, exiting\n", seconds);
    // The emulated game keeps detached threads alive; running exit handlers
    // here would tear down stdio and C++ statics underneath them.  Flush what
    // we produced and terminate immediately instead.
    fflush(NULL);
    _exit(0);
}
