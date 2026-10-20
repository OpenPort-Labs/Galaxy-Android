#pragma once

#include <jni.h>
#include <mutex>
#include <string>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "CookerBridge", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "CookerBridge", __VA_ARGS__)

namespace cooker_bridge {

extern JavaVM* gVm;
extern jobject gCallbackGlobal;
extern jmethodID gOnLogMethod;
extern jmethodID gOnProgressMethod;
extern std::mutex sOperationMutex;

void sendLogToJava(const char* text);
void sendProgressToJava(int percent, const char* text);
bool bindCallbackMethods(JNIEnv* env, jobject callback);

} // namespace cooker_bridge
