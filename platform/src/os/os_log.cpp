#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mutex>

#include "port/port.h"
#include "revolution/gx.h"
#include "revolution/os.h"
#include "revolution/os/OSError.h"
#include "revolution/os/OSReset.h"
#include "revolution/os/OSResetSW.h"

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "PetariVR"
#endif

extern "C" int port_log_to_stderr = 0;

static std::mutex sLogFileLock;
static FILE* sLogFile = nullptr;
static long sLogFileBytes = 0;
static const long kLogFileMax = 16 << 20;

extern "C" void port_log_file(const char* path) {
    char prev[512];
    snprintf(prev, sizeof(prev), "%s.prev", path);
    rename(path, prev);
    std::lock_guard<std::mutex> lock(sLogFileLock);
    sLogFile = fopen(path, "w");
    if (sLogFile) {
        setvbuf(sLogFile, nullptr, _IOLBF, 1024);
    }
}

static std::mutex sLogRingLock;
static constexpr int kMaxLogRing = 100;
static constexpr int kMaxLogLineLen = 256;
static char sLogRing[kMaxLogRing][kMaxLogLineLen];
static int sLogRingStart = 0;
static int sLogRingCount = 0;

extern "C" int port_get_recent_logs(char* out, int maxBytes) {
    if (!out || maxBytes <= 0) return 0;
    std::lock_guard<std::mutex> lock(sLogRingLock);
    out[0] = '\0';
    int written = 0;
    for (int i = 0; i < sLogRingCount; i++) {
        int idx = (sLogRingStart + i) % kMaxLogRing;
        int len = (int)strlen(sLogRing[idx]);
        if (written + len + 2 >= maxBytes) break;
        memcpy(out + written, sLogRing[idx], len);
        written += len;
        out[written++] = '\n';
        out[written] = '\0';
    }
    return written;
}

static void logToFile(const char* fmt, va_list args) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    char lineBuf[256];
    va_list ringCopy;
    va_copy(ringCopy, args);
    vsnprintf(lineBuf, sizeof(lineBuf), fmt, ringCopy);
    va_end(ringCopy);

    {
        std::lock_guard<std::mutex> lock(sLogRingLock);
        int slot = (sLogRingStart + sLogRingCount) % kMaxLogRing;
        if (sLogRingCount >= kMaxLogRing) {
            slot = sLogRingStart;
            sLogRingStart = (sLogRingStart + 1) % kMaxLogRing;
        } else {
            sLogRingCount++;
        }
        snprintf(sLogRing[slot], kMaxLogLineLen, "%02d:%02d:%02d.%03d %s",
                 tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(ts.tv_nsec / 1000000), lineBuf);
    }

    std::lock_guard<std::mutex> lock(sLogFileLock);
    if (!sLogFile || sLogFileBytes > kLogFileMax) {
        return;
    }
    int n = fprintf(sLogFile, "%02d:%02d:%02d.%03d ", tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(ts.tv_nsec / 1000000));
    n += vfprintf(sLogFile, fmt, args);
    fputc('\n', sLogFile);
    sLogFileBytes += n > 0 ? n + 1 : 1;
}

extern "C" void port_vlog(const char* fmt, va_list args) {
    {
        va_list copy;
        va_copy(copy, args);
        logToFile(fmt, copy);
        va_end(copy);
    }
#ifdef __ANDROID__
    if (port_log_to_stderr) {
        va_list copy;
        va_copy(copy, args);
        vfprintf(stderr, fmt, copy);
        fputc('\n', stderr);
        va_end(copy);
    }
    __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, fmt, args);
#else
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
#endif
}

extern "C" void port_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    port_vlog(fmt, args);
    va_end(args);
}

extern "C" void port_fatal(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    {
        va_list copy;
        va_copy(copy, args);
        logToFile(fmt, copy);
        va_end(copy);
    }
#ifdef __ANDROID__
    if (port_log_to_stderr) {
        va_list copy;
        va_copy(copy, args);
        fputs("FATAL: ", stderr);
        vfprintf(stderr, fmt, copy);
        fputc('\n', stderr);
        va_end(copy);
    }
    __android_log_vprint(ANDROID_LOG_FATAL, LOG_TAG, fmt, args);
#else
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
#endif
    va_end(args);
    abort();
}

extern "C" {

void OSVReport(const char* fmt, va_list args) {
    char buf[1024];
    port_vsnprintf(buf, sizeof(buf), fmt, args);
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = 0;
    }
    if (n) {
        port_log("[OS] %s", buf);
    }
}

void OSReport(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    OSVReport(fmt, args);
    va_end(args);
}

void OSPanic(const char* file, int line, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    port_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    port_fatal("OSPanic %s:%d: %s", file, line, buf);
}

void OSFatal(GXColor fg, GXColor bg, const char* msg) {
    (void)fg;
    (void)bg;
    port_fatal("OSFatal: %s", msg);
}

OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler) {
    (void)error;
    (void)handler;
    return nullptr;
}

void OSRegisterVersion(const char* version) { port_log("OSRegisterVersion: %s", version); }

void OSRegisterShutdownFunction(OSShutdownFunctionInfo* info) { (void)info; }

OSPowerCallback OSSetPowerCallback(OSPowerCallback cb) {
    (void)cb;
    return nullptr;
}

BOOL OSGetResetButtonState(void) { return FALSE; }
BOOL OSGetResetSwitchState(void) { return FALSE; }
void OSRestart(u32 code) { port_fatal("OSRestart(%u)", code); }
void OSRebootSystem(void) { port_fatal("OSRebootSystem"); }
void OSReturnToMenu(void) { port_fatal("OSReturnToMenu"); }
void OSShutdownSystem(void) { port_fatal("OSShutdownSystem"); }
void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu) {
    (void)forceMenu;
    port_fatal("OSResetSystem(%d, %u)", reset, resetCode);
}

void OSInit(void) {}

}  // extern "C"
