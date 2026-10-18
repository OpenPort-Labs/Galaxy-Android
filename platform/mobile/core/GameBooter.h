#pragma once

#include <android_native_app_glue.h>
#include <string>

namespace mobile_core {

class GameBooter {
public:
    // Checks for game files and boots the engine once assets are ready
    static void pollAndBoot(bool& gameBooted, std::string& dataRoot, const std::string& saveRoot,
                           const std::string& extDir, android_app* app);
};

}  // namespace mobile_core
