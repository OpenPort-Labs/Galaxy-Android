#include "GameBooter.h"

#include <android/log.h>
#include <ctype.h>
#include <sys/system_properties.h>

#include "../system/StorageManager.h"
#include "../system/SystemManager.h"
#include "../ui/UIOverlay.h"
#include "port/port.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "GalaxyBoot", __VA_ARGS__)

extern "C" void port_boot(const char* dataRoot, const char* saveRoot);

namespace mobile_core {

void GameBooter::pollAndBoot(bool& gameBooted, std::string& dataRoot, const std::string& saveRoot,
                             const std::string& extDir, android_app* app) {
    if (gameBooted) return;
    if (!dataRoot.empty()) {
        int userLang = mobile_ui::UIOverlay::get().settings().languageChoice;
        if (userLang == 1) port_language_set("en");
        else if (userLang == 2) port_language_set("es");
        else if (userLang == 3) port_language_set("fr");
        else if (userLang == 4) port_language_set("de");
        else if (userLang == 5) port_language_set("it");
        else {
            char sysLocale[PROP_VALUE_MAX] = {0};
            if (__system_property_get("persist.sys.locale", sysLocale) > 0 ||
                __system_property_get("ro.product.locale", sysLocale) > 0) {
                char langCode[3] = { (char)tolower(sysLocale[0]), (char)tolower(sysLocale[1]), 0 };
                port_language_set(langCode);
                LOGI("Auto-detected device language: %s -> set: %s", sysLocale, langCode);
            }
        }

        LOGI("Booting game from: %s", dataRoot.c_str());
        gameBooted = true;
        port_boot(dataRoot.c_str(), saveRoot.c_str());
    } else {
        static int64_t lastLog = 0;
        static int missingCount = 0;
        int64_t now = port_host_time_ns();
        if (now - lastLog > 2000000000ll) {
            lastLog = now;
            missingCount++;
            LOGI("Waiting for game files in /sdcard/GalaxyData or %s/game (attempt %d)", extDir.c_str(), missingCount);
            dataRoot = mobile_system::StorageManager::findGameData(extDir);
            if (dataRoot.empty() && missingCount >= 2) {
                mobile_system::SystemManager::returnToLauncher(app);
            }
        }
    }
}

}  // namespace mobile_core
