// OS time base, alarms (the decrementer), and miscellaneous OS services.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "port/port.h"
#include "revolution/os.h"
#include "revolution/os/OSError.h"
#include "revolution/os/OSReset.h"
#include "revolution/os/OSResetSW.h"

// ---------------------------------------------------------------------------
// Time base.  OSTime counts timer ticks (bus clock / 4 = 60.75 MHz) since
// 2000-01-01 00:00:00, like the console after the IPL loads the RTC.
// ---------------------------------------------------------------------------
static constexpr int64_t kTimerClock = 243000000 / 4;
static int64_t sBootNs;
static int64_t sBootTicks;

extern "C" int64_t port_host_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

extern "C" void port_host_sleep_ns(int64_t ns) {
    if (ns <= 0) {
        return;
    }
    struct timespec ts;
    ts.tv_sec = (time_t)(ns / 1000000000ll);
    ts.tv_nsec = (long)(ns % 1000000000ll);
    nanosleep(&ts, nullptr);
}

static inline int64_t nsToTicks(int64_t ns) { return (ns / 4000) * 243 + ((ns % 4000) * 243) / 4000; }
static inline int64_t ticksToNs(int64_t ticks) { return (ticks / 243) * 4000 + ((ticks % 243) * 4000) / 243; }

static void initTimeBase() {
    sBootNs = port_host_time_ns();
    // Seconds from 2000-01-01 to now, in local time (the Wii RTC is local).
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    int64_t localNow = (int64_t)now + lt.tm_gmtoff;
    const int64_t kEpoch2000 = 946684800;  // 2000-01-01 in Unix time
    sBootTicks = (localNow - kEpoch2000) * kTimerClock;
}

static inline OSTime rawTime() { return sBootTicks + nsToTicks(port_host_time_ns() - sBootNs); }

extern "C" {

OSTime OSGetTime(void) {
    port_irq_poll();
    return rawTime();
}

OSTick OSGetTick(void) {
    port_irq_poll();
    return (OSTick)rawTime();
}

OSTime __OSGetSystemTime(void) { return rawTime(); }
OSTime __OSTimeToSystemTime(OSTime t) { return t; }

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* td) {
    int64_t totalUs = ticks / 243 * 4 + (ticks % 243) * 4 / 243;  // 60.75 ticks per microsecond
    int64_t secs = totalUs / 1000000;
    int64_t usecs = totalUs % 1000000;
    if (usecs < 0) {
        usecs += 1000000;
        secs -= 1;
    }
    time_t t = (time_t)(secs + 946684800);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    td->sec = tmv.tm_sec;
    td->min = tmv.tm_min;
    td->hour = tmv.tm_hour;
    td->mday = tmv.tm_mday;
    td->mon = tmv.tm_mon;
    td->year = tmv.tm_year + 1900;
    td->wday = tmv.tm_wday;
    td->yday = tmv.tm_yday;
    td->msec = (int)(usecs / 1000);
    td->usec = (int)(usecs % 1000);
}

}  // extern "C"

// ---------------------------------------------------------------------------
// Alarms.  The queue is owned by the CPU; a host timer thread raises the
// alarm interrupt when the earliest deadline passes.
// ---------------------------------------------------------------------------
static OSAlarm* sAlarmHead;
static std::mutex sTimerMutex;
static std::condition_variable sTimerCv;
static int64_t sTimerDeadline = INT64_MAX;  // in OSTime ticks, guarded by sTimerMutex

static void rearmTimer() {
    std::lock_guard<std::mutex> lk(sTimerMutex);
    sTimerDeadline = sAlarmHead ? sAlarmHead->fire : INT64_MAX;
    sTimerCv.notify_all();
}

static void timerThread() {
    std::unique_lock<std::mutex> lk(sTimerMutex);
    for (;;) {
        if (sTimerDeadline == INT64_MAX) {
            sTimerCv.wait(lk);
            continue;
        }
        int64_t now = rawTime();
        if (now >= sTimerDeadline) {
            sTimerDeadline = INT64_MAX;  // re-armed by the handler
            lk.unlock();
            port_irq_raise(PORT_IRQ_ALARM);
            lk.lock();
            continue;
        }
        int64_t waitNs = ticksToNs(sTimerDeadline - now);
        sTimerCv.wait_for(lk, std::chrono::nanoseconds(waitNs));
    }
}

static void insertAlarm(OSAlarm* alarm) {
    OSAlarm* prev = nullptr;
    OSAlarm* next = sAlarmHead;
    while (next && next->fire <= alarm->fire) {
        prev = next;
        next = next->next;
    }
    alarm->prev = prev;
    alarm->next = next;
    if (next) {
        next->prev = alarm;
    }
    if (prev) {
        prev->next = alarm;
    } else {
        sAlarmHead = alarm;
    }
}

static void removeAlarm(OSAlarm* alarm) {
    if (alarm->next) {
        alarm->next->prev = alarm->prev;
    }
    if (alarm->prev) {
        alarm->prev->next = alarm->next;
    } else if (sAlarmHead == alarm) {
        sAlarmHead = alarm->next;
    }
    alarm->prev = alarm->next = nullptr;
}

static void alarmIrq() {
    OSTime now = rawTime();
    while (sAlarmHead && sAlarmHead->fire <= now) {
        OSAlarm* alarm = sAlarmHead;
        removeAlarm(alarm);
        OSAlarmHandler handler = alarm->handler;
        if (alarm->period) {
            alarm->fire += alarm->period;
            if (alarm->fire <= now) {
                // Skip missed periods rather than firing a burst.
                OSTime missed = (now - alarm->fire) / alarm->period + 1;
                alarm->fire += missed * alarm->period;
            }
            insertAlarm(alarm);
        } else {
            alarm->handler = nullptr;
        }
        if (handler) {
            handler(alarm, OSGetCurrentContext());
        }
    }
    rearmTimer();
}

extern "C" {

void OSCreateAlarm(OSAlarm* alarm) {
    alarm->handler = nullptr;
    alarm->tag = 0;
    alarm->prev = alarm->next = nullptr;
}

void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler) {
    BOOL en = OSDisableInterrupts();
    if (alarm->handler) {
        removeAlarm(alarm);
    }
    alarm->period = 0;
    alarm->handler = handler;
    alarm->fire = rawTime() + tick;
    insertAlarm(alarm);
    rearmTimer();
    OSRestoreInterrupts(en);
}

void OSSetAbsAlarm(OSAlarm* alarm, OSTime time, OSAlarmHandler handler) {
    BOOL en = OSDisableInterrupts();
    if (alarm->handler) {
        removeAlarm(alarm);
    }
    alarm->period = 0;
    alarm->handler = handler;
    alarm->fire = time;
    insertAlarm(alarm);
    rearmTimer();
    OSRestoreInterrupts(en);
}

void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period, OSAlarmHandler handler) {
    BOOL en = OSDisableInterrupts();
    if (alarm->handler) {
        removeAlarm(alarm);
    }
    alarm->period = period;
    alarm->start = start;
    alarm->handler = handler;
    OSTime now = rawTime();
    if (period > 0 && start < now) {
        alarm->fire = start + period * ((now - start) / period + 1);
    } else {
        alarm->fire = start;
    }
    insertAlarm(alarm);
    rearmTimer();
    OSRestoreInterrupts(en);
}

void OSCancelAlarm(OSAlarm* alarm) {
    BOOL en = OSDisableInterrupts();
    if (alarm->handler) {
        removeAlarm(alarm);
        alarm->handler = nullptr;
        rearmTimer();
    }
    OSRestoreInterrupts(en);
}

void OSCancelAlarms(u32 tag) {
    BOOL en = OSDisableInterrupts();
    for (OSAlarm* a = sAlarmHead; a;) {
        OSAlarm* n = a->next;
        if (a->tag == tag) {
            removeAlarm(a);
            a->handler = nullptr;
        }
        a = n;
    }
    rearmTimer();
    OSRestoreInterrupts(en);
}

void OSSetAlarmTag(OSAlarm* alarm, u32 tag) { alarm->tag = tag; }
void OSSetAlarmUserData(OSAlarm* alarm, void* data) { alarm->userData = data; }
void* OSGetAlarmUserData(const OSAlarm* alarm) { return alarm->userData; }

}  // extern "C"

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------
extern "C" void port_os_time_init(void) {
    initTimeBase();
    port_irq_set_handler(PORT_IRQ_ALARM, alarmIrq);
    std::thread(timerThread).detach();
}
