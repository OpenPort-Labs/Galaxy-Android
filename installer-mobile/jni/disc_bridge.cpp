#include "bridge_common.h"
#include "wii_extractor.h"

using namespace cooker_bridge;

extern "C" JNIEXPORT jboolean JNICALL
Java_com_galaxy_installer_CookerEngine_isWiiDisc(
        JNIEnv *env, jclass clazz, jstring jFilePath) {
    if (!jFilePath) return JNI_FALSE;
    const char* filePath = env->GetStringUTFChars(jFilePath, nullptr);
    if (!filePath) return JNI_FALSE;
    bool result = wii::isWiiDiscImage(filePath);
    env->ReleaseStringUTFChars(jFilePath, filePath);
    return result ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_galaxy_installer_CookerEngine_extractDisc(
        JNIEnv *env, jclass clazz,
        jstring jImagePath, jstring jOutDir,
        jboolean jIncludeMovies, jobject jCallback) {

    std::unique_lock<std::mutex> opLock(sOperationMutex, std::try_to_lock);
    if (!opLock.owns_lock()) {
        LOGE("extractDisc: another installer operation is already running");
        return -2;
    }

    const char* imagePath = env->GetStringUTFChars(jImagePath, nullptr);
    const char* outDir = env->GetStringUTFChars(jOutDir, nullptr);
    if (!imagePath || !outDir) {
        LOGE("extractDisc: a null path argument was passed in");
        return -1;
    }
    bool includeMovies = (bool)jIncludeMovies;

    gCallbackGlobal = env->NewGlobalRef(jCallback);
    if (!bindCallbackMethods(env, jCallback)) {
        env->DeleteGlobalRef(gCallbackGlobal);
        gCallbackGlobal = nullptr;
        env->ReleaseStringUTFChars(jImagePath, imagePath);
        env->ReleaseStringUTFChars(jOutDir, outDir);
        return -1;
    }

    bool success = wii::extractWiiDisc(
        imagePath, outDir, includeMovies,
        [](const std::string& msg) {
            sendLogToJava(msg.c_str());
        },
        [](int pct, const std::string& msg) {
            sendProgressToJava(pct, msg.c_str());
        }
    );

    env->ReleaseStringUTFChars(jImagePath, imagePath);
    env->ReleaseStringUTFChars(jOutDir, outDir);
    env->DeleteGlobalRef(gCallbackGlobal);
    gCallbackGlobal = nullptr;

    return success ? 0 : 1;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_openport_galaxyandroid_CookerEngine_isWiiDisc(
        JNIEnv *env, jclass clazz, jstring jFilePath) {
    return Java_com_galaxy_installer_CookerEngine_isWiiDisc(env, clazz, jFilePath);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_openport_galaxyandroid_CookerEngine_extractDisc(
        JNIEnv *env, jclass clazz,
        jstring jImagePath, jstring jOutDir,
        jboolean jIncludeMovies, jobject jCallback) {
    return Java_com_galaxy_installer_CookerEngine_extractDisc(env, clazz, jImagePath, jOutDir, jIncludeMovies, jCallback);
}
