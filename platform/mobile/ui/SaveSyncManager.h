#pragma once

#include <string>

namespace mobile_ui {

// Android storage locations, injectable so host tests exercise the real
// import/export path instead of a parallel implementation.
struct SaveIoPaths {
    std::string downloadDir = "/sdcard/Download";
    std::string dolphinDir = "/sdcard/dolphin-emu/Wii";
    std::string mirrorDir = "/sdcard/GalaxyData/saves";
};

class SaveSyncManager {
public:
    // Exports active game save data to the device Download folder
    static bool exportSave(const std::string& savePath, std::string& outToastMsg,
                           const SaveIoPaths& paths = SaveIoPaths());

    // Imports game save data from the device Download folder
    static bool importSave(const std::string& savePath, std::string& outToastMsg,
                           const SaveIoPaths& paths = SaveIoPaths());
};

}  // namespace mobile_ui
