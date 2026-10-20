#include "bridge_common.h"

namespace cooker_bridge {

JavaVM* gVm = nullptr;
jobject gCallbackGlobal = nullptr;
jmethodID gOnLogMethod = nullptr;
jmethodID gOnProgressMethod = nullptr;
std::mutex sOperationMutex;

void sendLogToJava(const char* text) {
    if (!gVm || !gCallbackGlobal || !gOnLogMethod) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (gVm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (gVm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
        }
    }
    if (env) {
        jstring jText = env->NewStringUTF(text ? text : "");
        env->CallVoidMethod(gCallbackGlobal, gOnLogMethod, jText);
        env->DeleteLocalRef(jText);
        if (attached) {
            gVm->DetachCurrentThread();
        }
    }
}

void sendProgressToJava(int percent, const char* text) {
    if (!gVm || !gCallbackGlobal || !gOnProgressMethod) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (gVm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (gVm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
        }
    }
    if (env) {
        jstring jText = env->NewStringUTF(text ? text : "");
        env->CallVoidMethod(gCallbackGlobal, gOnProgressMethod, percent, jText);
        env->DeleteLocalRef(jText);
        if (attached) {
            gVm->DetachCurrentThread();
        }
    }
}

bool bindCallbackMethods(JNIEnv* env, jobject callback) {
    if (!callback) return false;
    jclass cbClass = env->GetObjectClass(callback);
    gOnLogMethod = env->GetMethodID(cbClass, "onLog", "(Ljava/lang/String;)V");
    gOnProgressMethod = env->GetMethodID(cbClass, "onProgress", "(ILjava/lang/String;)V");
    env->DeleteLocalRef(cbClass);
    return gOnLogMethod != nullptr && gOnProgressMethod != nullptr;
}

} // namespace cooker_bridge
