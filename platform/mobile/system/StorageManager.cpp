#include "StorageManager.h"

#include <android/log.h>
#include <dirent.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <vector>

#include "port/port.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GalaxyStorage", __VA_ARGS__)

namespace mobile_system {

bool StorageManager::isDir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool StorageManager::checkGameFolder(const std::string& dir, bool* ready) {
    struct stat st;
    if (stat((dir + "/sys/fst.bin").c_str(), &st) != 0 || !S_ISREG(st.st_mode) || !isDir(dir + "/files")) {
        return false;
    }
    char magic[4] = {};
    FILE* f = fopen((dir + "/files/ObjectData/Coin.arc").c_str(), "rb");
    bool converted = f && fread(magic, 1, 4, f) == 4 && memcmp(magic, "CRAR", 4) == 0;
    if (f) fclose(f);

    bool dolData = true;
    for (const char* name : {"ErrorMessageArchive.arc", "StoryEvent.bcsv", "GalaxyID.bcsv"}) {
        dolData = dolData && stat((dir + "/sys/" + name).c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }
    bool known = port_dvd_identify(dir.c_str()) != nullptr;
    if (ready) *ready = converted && dolData && known;
    return true;
}

std::string StorageManager::findGameData(const std::string& extDir) {
    std::vector<std::string> candidates = {
        "/sdcard/GalaxyData",
        "/sdcard/GalaxyData/game",
        "/storage/emulated/0/GalaxyData",
        "/storage/emulated/0/GalaxyData/game",
        extDir + "/game",
        extDir + "/GalaxyData",
        extDir,
        "/sdcard/Android/data/com.openport.galaxyandroid/files/game",
        "/sdcard/Android/data/com.openport.galaxyandroid/files/GalaxyData",
        "/sdcard/Android/data/com.galaxy.mobile.plus/files/game",
        "/sdcard/Android/data/com.galaxy.mobile/files/game",
        "/sdcard/Android/data/com.galaxy.quest/files/game",
        "/sdcard/Galaxy/game",
        "/sdcard/GalaxyQuest",
        "/sdcard/GalaxyQuest/game",
        "/sdcard/Download/game",
        "/sdcard/Download/cooked",
        "/sdcard/Download/GalaxyQuest",
        "/sdcard/Documents/game",
        "/sdcard/Documents/cooked",
        "/sdcard/game",
        "/sdcard/cooked",
        "/data/local/tmp/petari/game"
    };

    if (const char* envRoot = getenv("PETARI_GAME_ROOT")) {
        candidates.insert(candidates.begin(), envRoot);
    }

    // Discover external MicroSD card mount points (/storage/XXXX-XXXX)
    DIR* storageDir = opendir("/storage");
    if (storageDir) {
        struct dirent* entry;
        while ((entry = readdir(storageDir)) != nullptr) {
            if (entry->d_name[0] != '.' && strcmp(entry->d_name, "emulated") != 0 && strcmp(entry->d_name, "self") != 0) {
                std::string sd = std::string("/storage/") + entry->d_name;
                candidates.push_back(sd + "/GalaxyData");
                candidates.push_back(sd + "/GalaxyData/game");
                candidates.push_back(sd + "/Android/data/com.openport.galaxyandroid/files/GalaxyData");
                candidates.push_back(sd + "/Android/data/com.openport.galaxyandroid/files/game");
                candidates.push_back(sd + "/game");
                candidates.push_back(sd + "/cooked");
            }
        }
        closedir(storageDir);
    }

    DIR* mediaDir = opendir("/mnt/media_rw");
    if (mediaDir) {
        struct dirent* entry;
        while ((entry = readdir(mediaDir)) != nullptr) {
            if (entry->d_name[0] != '.') {
                std::string sd = std::string("/mnt/media_rw/") + entry->d_name;
                candidates.push_back(sd + "/GalaxyData");
                candidates.push_back(sd + "/GalaxyData/game");
                candidates.push_back(sd + "/Android/data/com.openport.galaxyandroid/files/GalaxyData");
                candidates.push_back(sd + "/Android/data/com.openport.galaxyandroid/files/game");
                candidates.push_back(sd + "/game");
            }
        }
        closedir(mediaDir);
    }

    for (const auto& path : candidates) {
        bool ready = false;
        if (checkGameFolder(path, &ready) && ready) {
            LOGI("Found valid game data at: %s", path.c_str());
            return path;
        }
    }
    return "";
}

void StorageManager::promptStoragePermissionIfMissing(ANativeActivity* activity) {
    if (!activity || !activity->vm || !activity->clazz) return;
    JNIEnv* env = nullptr;
    if (activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;

    if (env->ExceptionCheck()) env->ExceptionClear();

    jclass versionClass = env->FindClass("android/os/Build$VERSION");
    if (!versionClass) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }
    jfieldID sdkIntField = env->GetStaticFieldID(versionClass, "SDK_INT", "I");
    if (!sdkIntField) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(versionClass);
        return;
    }
    jint sdkInt = env->GetStaticIntField(versionClass, sdkIntField);
    env->DeleteLocalRef(versionClass);

    if (sdkInt >= 30) {
        jclass envClass = env->FindClass("android/os/Environment");
        if (!envClass) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            return;
        }
        jmethodID isManagerMethod = env->GetStaticMethodID(envClass, "isExternalStorageManager", "()Z");
        if (!isManagerMethod) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(envClass);
            return;
        }
        jboolean isManager = env->CallStaticBooleanMethod(envClass, isManagerMethod);
        env->DeleteLocalRef(envClass);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return;
        }

        if (!isManager) {
            LOGI("MANAGE_EXTERNAL_STORAGE permission missing, launching settings prompt...");
            jclass intentClass = env->FindClass("android/content/Intent");
            if (!intentClass) {
                if (env->ExceptionCheck()) env->ExceptionClear();
                return;
            }
            jmethodID intentCtor = env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V");

            jclass uriClass = env->FindClass("android/net/Uri");
            if (!uriClass || !intentCtor) {
                if (env->ExceptionCheck()) env->ExceptionClear();
                if (intentClass) env->DeleteLocalRef(intentClass);
                if (uriClass) env->DeleteLocalRef(uriClass);
                return;
            }
            jmethodID parseMethod = env->GetStaticMethodID(uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");

            jclass actClass = env->GetObjectClass(activity->clazz);
            if (!actClass || !parseMethod) {
                if (env->ExceptionCheck()) env->ExceptionClear();
                env->DeleteLocalRef(intentClass);
                env->DeleteLocalRef(uriClass);
                if (actClass) env->DeleteLocalRef(actClass);
                return;
            }
            jmethodID getPackageNameMethod = env->GetMethodID(actClass, "getPackageName", "()Ljava/lang/String;");
            jmethodID startActivityMethod = env->GetMethodID(actClass, "startActivity", "(Landroid/content/Intent;)V");

            if (getPackageNameMethod && startActivityMethod) {
                jstring pkgName = (jstring)env->CallObjectMethod(activity->clazz, getPackageNameMethod);
                if (pkgName) {
                    const char* pkgChars = env->GetStringUTFChars(pkgName, nullptr);
                    std::string uriStr = std::string("package:") + (pkgChars ? pkgChars : "");
                    if (pkgChars) env->ReleaseStringUTFChars(pkgName, pkgChars);

                    jstring jUriStr = env->NewStringUTF(uriStr.c_str());
                    jobject jUri = env->CallStaticObjectMethod(uriClass, parseMethod, jUriStr);

                    jstring actionStr = env->NewStringUTF("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION");
                    jobject intent = env->NewObject(intentClass, intentCtor, actionStr, jUri);

                    if (intent) {
                        env->CallVoidMethod(activity->clazz, startActivityMethod, intent);
                        env->DeleteLocalRef(intent);
                    }
                    if (actionStr) env->DeleteLocalRef(actionStr);
                    if (jUri) env->DeleteLocalRef(jUri);
                    if (jUriStr) env->DeleteLocalRef(jUriStr);
                    env->DeleteLocalRef(pkgName);
                }
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(actClass);
            env->DeleteLocalRef(uriClass);
            env->DeleteLocalRef(intentClass);
        }
    }
}

}  // namespace mobile_system
