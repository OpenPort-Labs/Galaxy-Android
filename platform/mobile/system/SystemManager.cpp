#include "SystemManager.h"

#include <android/log.h>
#include <jni.h>
#include <math.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GalaxySystem", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GalaxySystem", __VA_ARGS__)

namespace mobile_system {

namespace {

static void crashSignalHandler(int sig, siginfo_t* info, void* ucontext) {
    (void)ucontext;
    const char* sigName = "UNKNOWN";
    if (sig == SIGSEGV) sigName = "SIGSEGV (Segmentation Fault)";
    else if (sig == SIGBUS) sigName = "SIGBUS (Bus Error / Alignment)";
    else if (sig == SIGFPE) sigName = "SIGFPE (Arithmetic Exception)";
    else if (sig == SIGILL) sigName = "SIGILL (Illegal Instruction)";
    else if (sig == SIGABRT) sigName = "SIGABRT (Abort)";

    LOGE("================ FATAL NATIVE CRASH ================");
    LOGE("Signal: %d (%s) at address %p", sig, sigName, info ? info->si_addr : nullptr);

    FILE* f = fopen("/sdcard/GalaxyData/galaxy_crash.log", "a");
    if (f) {
        time_t t = time(nullptr);
        char* ct = ctime(&t);
        if (ct) {
            char* nl = strchr(ct, '\n');
            if (nl) *nl = '\0';
        }
        fprintf(f, "[%s] FATAL SIGNAL %d (%s) at address %p\n",
                ct ? ct : "unknown time", sig, sigName, info ? info->si_addr : nullptr);
        fflush(f);
        fclose(f);
    }
    sync();

    signal(sig, SIG_DFL);
    raise(sig);
}

}  // namespace

void SystemManager::installCrashHandlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crashSignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
}

void SystemManager::pinThreadToPerformanceCores() {
    // Artificial CPU core pinning is disabled to allow Android EAS (Energy Aware
    // Scheduler) to dynamically migrate threads across big and LITTLE cores
    // without thermal lockups (especially on MediaTek Helio G99 / 2x A76 + 6x A55).
}

void SystemManager::elevateThreadPriority(int priority) {
    setpriority(PRIO_PROCESS, 0, priority);
}

void SystemManager::triggerHaptic(android_app* app, int ms) {
    if (ms <= 0 || !app || !app->activity || !app->activity->vm || !app->activity->clazz) return;
    JavaVM* vm = app->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    jint getEnvRes = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvRes == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
        attached = true;
    } else if (getEnvRes != JNI_OK || !env) {
        return;
    }

    if (env->ExceptionCheck()) env->ExceptionClear();
    jclass clazz = env->GetObjectClass(app->activity->clazz);
    if (clazz) {
        jmethodID mid = env->GetStaticMethodID(clazz, "triggerHaptic", "(I)V");
        if (mid) {
            env->CallStaticVoidMethod(clazz, mid, (jint)ms);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(clazz);
    }
    if (attached) vm->DetachCurrentThread();
}

void SystemManager::returnToLauncher(android_app* app) {
    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz) return;
    JavaVM* vm = app->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    jint getEnvRes = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvRes == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
        attached = true;
    } else if (getEnvRes != JNI_OK || !env) {
        return;
    }

    if (env->ExceptionCheck()) env->ExceptionClear();
    jclass clazz = env->GetObjectClass(app->activity->clazz);
    if (clazz) {
        jmethodID mid = env->GetStaticMethodID(clazz, "returnToLauncher", "()V");
        if (mid) {
            env->CallStaticVoidMethod(clazz, mid);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(clazz);
    }
    if (attached) vm->DetachCurrentThread();
}

void SystemManager::setPreferredRefreshRate(android_app* app, float targetFps) {
    static float sLastAppliedFps = -1.0f;
    if (fabsf(sLastAppliedFps - targetFps) < 0.5f) return;
    sLastAppliedFps = targetFps;

    if (!app || !app->activity || !app->activity->vm || !app->activity->clazz) return;
    JavaVM* vm = app->activity->vm;
    JNIEnv* env = nullptr;
    bool attached = false;
    jint getEnvRes = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvRes == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;
        attached = true;
    } else if (getEnvRes != JNI_OK || !env) {
        return;
    }

    if (env->ExceptionCheck()) env->ExceptionClear();
    jclass clazz = env->GetObjectClass(app->activity->clazz);
    if (clazz) {
        jmethodID mid = env->GetStaticMethodID(clazz, "setPreferredRefreshRate", "(F)V");
        if (mid) {
            env->CallStaticVoidMethod(clazz, mid, (jfloat)targetFps);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(clazz);
    }
    if (attached) vm->DetachCurrentThread();
}

void SystemManager::enforceOrientation(android_app* app, int orientationChoice) {
    if (!app || !app->activity) return;
    int requested = 6;  // SCREEN_ORIENTATION_SENSOR_LANDSCAPE
    if (orientationChoice == 1) requested = 0;       // SCREEN_ORIENTATION_LANDSCAPE
    else if (orientationChoice == 2) requested = 8;  // SCREEN_ORIENTATION_REVERSE_LANDSCAPE

    ANativeActivity* act = app->activity;
    JNIEnv* env = nullptr;
    act->vm->AttachCurrentThread(&env, nullptr);
    if (env) {
        jclass actClass = env->GetObjectClass(act->clazz);
        if (actClass) {
            jmethodID setOrient = env->GetMethodID(actClass, "setRequestedOrientation", "(I)V");
            if (setOrient) {
                env->CallVoidMethod(act->clazz, setOrient, requested);
            }
            env->DeleteLocalRef(actClass);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        act->vm->DetachCurrentThread();
    }
}

}  // namespace mobile_system
