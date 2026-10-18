#pragma once

#include <string>

namespace mobile_ui {

class SaveSyncManager {
public:
    // Exports active game save data to the device Download folder
    static bool exportSave(const std::string& savePath, std::string& outToastMsg);

    // Imports game save data from the device Download folder
    static bool importSave(const std::string& savePath, std::string& outToastMsg);
};

}  // namespace mobile_ui
