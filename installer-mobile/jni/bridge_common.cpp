#include "bridge_common.h"

namespace cooker_bridge {

JavaVM* gVm = nullptr;
jobject gCallbackGlobal = nullptr;
jmethodID gOnLogMethod = nullptr;
jmethodID gOnProgressMethod = nullptr;
std::mutex sOperationMutex;

// A pending Java exception makes every later JNI call undefined, so callbacks
// clear theirs instead of letting it leak into the next bridge operation.
static bool clearJavaException(JNIEnv* env, const char* what) {
    if (!env || !env->ExceptionCheck()) return false;
    env->ExceptionClear();
    LOGE("cleared pending Java exception during %s", what);
    return true;
}

static JNIEnv* acquireEnv(bool* attached) {
    *attached = false;
    if (!gVm) return nullptr;
    JNIEnv* env = nullptr;
    if (gVm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) return env;
    if (gVm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    *attached = true;
    if (clearJavaException(env, "AttachCurrentThread")) {
        gVm->DetachCurrentThread();
        return nullptr;
    }
    return env;
}

void sendLogToJava(const char* text) {
    if (!gVm || !gCallbackGlobal || !gOnLogMethod) return;
    bool attached = false;
    JNIEnv* env = acquireEnv(&attached);
    if (env) {
        clearJavaException(env, "log entry");
        jstring jText = env->NewStringUTF(text ? text : "");
        if (!clearJavaException(env, "NewStringUTF") && jText) {
            env->CallVoidMethod(gCallbackGlobal, gOnLogMethod, jText);
            clearJavaException(env, "onLog");
        }
        if (jText) env->DeleteLocalRef(jText);
        if (attached) {
            gVm->DetachCurrentThread();
        }
    }
}

void sendProgressToJava(int percent, const char* text) {
    if (!gVm || !gCallbackGlobal || !gOnProgressMethod) return;
    bool attached = false;
    JNIEnv* env = acquireEnv(&attached);
    if (env) {
        clearJavaException(env, "progress entry");
        jstring jText = env->NewStringUTF(text ? text : "");
        if (!clearJavaException(env, "NewStringUTF") && jText) {
            env->CallVoidMethod(gCallbackGlobal, gOnProgressMethod, percent, jText);
            clearJavaException(env, "onProgress");
        }
        if (jText) env->DeleteLocalRef(jText);
        if (attached) {
            gVm->DetachCurrentThread();
        }
    }
}

bool bindCallbackMethods(JNIEnv* env, jobject callback) {
    if (!env || !callback) return false;
    gOnLogMethod = nullptr;
    gOnProgressMethod = nullptr;
    jclass cbClass = env->GetObjectClass(callback);
    if (!cbClass || clearJavaException(env, "GetObjectClass")) {
        if (cbClass) env->DeleteLocalRef(cbClass);
        return false;
    }
    gOnLogMethod = env->GetMethodID(cbClass, "onLog", "(Ljava/lang/String;)V");
    if (!gOnLogMethod && clearJavaException(env, "GetMethodID(onLog)")) {
        env->DeleteLocalRef(cbClass);
        return false;
    }
    gOnProgressMethod = env->GetMethodID(cbClass, "onProgress", "(ILjava/lang/String;)V");
    clearJavaException(env, "GetMethodID(onProgress)");
    env->DeleteLocalRef(cbClass);
    return gOnLogMethod != nullptr && gOnProgressMethod != nullptr;
}

} // namespace cooker_bridge
