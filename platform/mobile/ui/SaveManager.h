#pragma once

#include <stdint.h>
#include <string>

namespace mobile_ui {

class SaveManager {
public:
    SaveManager();

    void setSavePath(const std::string& path) { mSavePath = path; }
    const std::string& savePath() const { return mSavePath; }

    int currentSlot() const { return mCurrentSlot; }
    void setCurrentSlot(int slot) { mCurrentSlot = (slot >= 1 && slot <= 4) ? slot : 1; }

    // Triggers transactional quick save anywhere
    std::string triggerSaveAnywhere(bool haptics);

    // Triggers transactional save to slot 1-4
    std::string triggerSaveSlot(int slot, bool haptics);

    // Verifies and restores save from slot 1-4
    std::string triggerLoadSlot(int slot, bool haptics);

    // Per-frame tick to detect save completion and verify directory snapshot
    bool update(std::string& outToastMsg);

    // Export save to /sdcard/Download
    bool exportSave(std::string& outToastMsg);

    // Import save from /sdcard/Download
    bool importSave(std::string& outToastMsg);

private:
    std::string mSavePath;
    int mCurrentSlot = 1;

    // Transactional save state
    int mPendingSaveSlot = -1;
    uint64_t mPendingSaveStartGen = 0;
    int mPendingSaveWaitFrames = 0;
};

}  // namespace mobile_ui
