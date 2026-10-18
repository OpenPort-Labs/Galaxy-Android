#pragma once

#include <android_native_app_glue.h>
#include <string>

namespace mobile_system {

class StorageManager {
public:
    // Checks if path exists and is a directory
    static bool isDir(const std::string& path);

    // Verifies whether a folder contains extracted game files and converted CRAR archives
    static bool checkGameFolder(const std::string& dir, bool* ready = nullptr);

    // Scans standard external, internal, and MicroSD mount paths for Super Mario Galaxy data
    static std::string findGameData(const std::string& extDir);

    // Prompts user for Android 11+ MANAGE_EXTERNAL_STORAGE permission if not granted
    static void promptStoragePermissionIfMissing(ANativeActivity* activity);
};

}  // namespace mobile_system
