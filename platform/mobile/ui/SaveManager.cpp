#include "SaveManager.h"
#include "SaveStorage.h"
#include "SaveSyncManager.h"

#include <android/log.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MobileUI", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MobileUI", __VA_ARGS__)

extern "C" void port_trigger_quick_save(void);
extern "C" void port_trigger_quick_load(int slot);
extern "C" uint64_t port_get_save_generation(void);
extern "C" int port_is_save_in_progress(void);
extern "C" void port_android_vibrate(int ms);

namespace mobile_ui {

SaveManager::SaveManager() = default;

std::string SaveManager::triggerSaveAnywhere(bool haptics) {
    if (mPendingSaveSlot >= 0) return "A save is already in progress!";
    port_trigger_quick_save();
    mPendingSaveSlot = 0;
    mPendingSaveStartGen = port_get_save_generation();
    mPendingSaveWaitFrames = 0;
    if (haptics) port_android_vibrate(50);
    LOGI("Save anywhere requested; waiting for game sequence to commit");
    return "Saving game progress anywhere...";
}

std::string SaveManager::triggerSaveSlot(int slot, bool haptics) {
    if (slot < 1 || slot > 4) return "Invalid save slot!";
    if (mPendingSaveSlot >= 0) return "A save is already in progress!";
    port_trigger_quick_save();
    mPendingSaveSlot = slot;
    mPendingSaveStartGen = port_get_save_generation();
    mPendingSaveWaitFrames = 0;
    if (haptics) port_android_vibrate(50);
    LOGI("Save to slot %d requested; waiting for game sequence to commit", slot);
    char msg[64];
    snprintf(msg, sizeof(msg), "Saving to Slot %d...", slot);
    return std::string(msg);
}

std::string SaveManager::triggerLoadSlot(int slot, bool haptics) {
    if (slot < 1 || slot > 4) return "Invalid save slot!";
    if (mPendingSaveSlot >= 0 || port_is_save_in_progress())
        return "Game is saving; try again in a moment.";
    if (mSavePath.empty()) return "Save path not set!";
    size_t lastSlash = mSavePath.find_last_of('/');
    if (lastSlash == std::string::npos) return "Invalid save path!";
    std::string nandDir = mSavePath.substr(0, lastSlash);
    std::string slotSuffix = (slot == 4) ? "_quick" : ("_slot" + std::to_string(slot));
    std::string slotDir = nandDir + slotSuffix;

    if (!savefs::validateSaveDir(slotDir)) {
        std::string pubSlot = "/sdcard/GalaxyData/saves/" + ((slot == 4) ? std::string("quick") : ("slot" + std::to_string(slot)));
        if (savefs::validateSaveDir(pubSlot)) {
            slotDir = pubSlot;
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), (slot == 4) ? "Quick Save slot is empty!" : "Slot %d is empty or corrupted!", slot);
            return std::string(msg);
        }
    }

    std::string staging = nandDir + "_load_staging";
    std::string replaced = nandDir + "_load_old";
    savefs::removeDirRecursive(staging);
    savefs::removeDirRecursive(replaced);

    bool ok = savefs::copyDirVerified(slotDir, staging) && savefs::validateSaveDir(staging);
    if (ok && rename(nandDir.c_str(), replaced.c_str()) != 0) ok = false;
    if (ok && rename(staging.c_str(), nandDir.c_str()) != 0) {
        rename(replaced.c_str(), nandDir.c_str());
        ok = false;
    }
    if (ok && !savefs::validateSaveDir(nandDir)) {
        rename(nandDir.c_str(), staging.c_str());
        rename(replaced.c_str(), nandDir.c_str());
        ok = false;
    }
    if (!ok) {
        savefs::removeDirRecursive(staging);
        LOGE("Failed to restore slot %d; previous save kept in %s", slot, replaced.c_str());
        return "Slot restore failed! Preserved current save.";
    }

    savefs::removeDirRecursive(replaced);
    port_trigger_quick_load(slot);
    if (haptics) port_android_vibrate(75);
    char msg[64];
    snprintf(msg, sizeof(msg), (slot == 4) ? "Quick Save Loaded & Stage Reloaded!" : "Slot %d Loaded & Stage Reloaded!", slot);
    LOGI("NAND verified restored from slot %d: %s", slot, slotDir.c_str());
    return std::string(msg);
}

bool SaveManager::update(std::string& outToastMsg) {
    if (mPendingSaveSlot < 0) return false;

    mPendingSaveWaitFrames++;
    bool committed = port_get_save_generation() > mPendingSaveStartGen;
    bool idle = mPendingSaveWaitFrames > 6 && !port_is_save_in_progress();

    if (!committed && !idle) {
        if (mPendingSaveWaitFrames <= 120) return false;
        mPendingSaveSlot = -1;
        mPendingSaveWaitFrames = 0;
        outToastMsg = "Save did not finish; snapshot skipped.";
        LOGE("Save sequence exceeded the wait timeout; no snapshot taken");
        return true;
    }

    int slot = mPendingSaveSlot;
    mPendingSaveSlot = -1;
    mPendingSaveWaitFrames = 0;

    if (mSavePath.empty()) return false;
    size_t lastSlash = mSavePath.find_last_of('/');
    if (lastSlash == std::string::npos) return false;

    std::string nandDir = mSavePath.substr(0, lastSlash);
    mkdir("/sdcard/GalaxyData", 0775);
    mkdir("/sdcard/GalaxyData/saves", 0775);
    std::string pubCurrent = "/sdcard/GalaxyData/saves/current";

    if (slot == 0) {
        savefs::removeDirRecursive(pubCurrent);
        bool ok = savefs::copyDirVerified(nandDir, pubCurrent) && savefs::validateSaveDir(pubCurrent);
        outToastMsg = ok ? "Progress Saved Anywhere! (Verified)" : "Save snapshot failed verification!";
        if (!ok) LOGE("Save-anywhere snapshot verification failed");
        return true;
    }

    std::string slotSuffix = (slot == 4) ? "_quick" : ("_slot" + std::to_string(slot));
    std::string slotDir = nandDir + slotSuffix;
    std::string slotStaging = slotDir + "_staging";
    std::string slotOld = slotDir + "_old";
    savefs::removeDirRecursive(slotStaging);
    savefs::removeDirRecursive(slotOld);

    bool ok = savefs::copyDirVerified(nandDir, slotStaging) && savefs::validateSaveDir(slotStaging);
    if (ok && rename(slotDir.c_str(), slotOld.c_str()) != 0 && errno != ENOENT) ok = false;
    if (ok && rename(slotStaging.c_str(), slotDir.c_str()) != 0) {
        rename(slotOld.c_str(), slotDir.c_str());
        ok = false;
    }
    if (!ok) {
        savefs::removeDirRecursive(slotStaging);
        outToastMsg = "Save snapshot failed verification!";
        LOGE("Save snapshot verification failed for slot %d", slot);
        return true;
    }
    savefs::removeDirRecursive(slotOld);

    std::string pubSlot = "/sdcard/GalaxyData/saves/" + ((slot == 4) ? std::string("quick") : ("slot" + std::to_string(slot)));
    savefs::removeDirRecursive(pubSlot);
    if (!savefs::copyDirVerified(slotDir, pubSlot)) LOGE("Could not mirror slot %d to %s", slot, pubSlot.c_str());
    savefs::removeDirRecursive(pubCurrent);
    if (!savefs::copyDirVerified(slotDir, pubCurrent)) LOGE("Could not mirror slot %d to %s", slot, pubCurrent.c_str());

    char msg[96];
    snprintf(msg, sizeof(msg), (slot == 4) ? "Quick Saved Anywhere! (Verified)" : "Saved to Slot %d! (Verified)", slot);
    outToastMsg = std::string(msg);
    LOGI("NAND verified saved to slot %d: %s", slot, slotDir.c_str());
    return true;
}

bool SaveManager::exportSave(std::string& outToastMsg) {
    return SaveSyncManager::exportSave(mSavePath, outToastMsg);
}

bool SaveManager::importSave(std::string& outToastMsg) {
    return SaveSyncManager::importSave(mSavePath, outToastMsg);
}


}  // namespace mobile_ui
