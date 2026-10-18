#include "SaveSyncManager.h"
#include "DolphinSaveConverter.h"

#include <stdio.h>
#include <sys/stat.h>
#include <vector>

namespace mobile_ui {

bool SaveSyncManager::exportSave(const std::string& savePath, std::string& outToastMsg) {
    if (savePath.empty()) return false;
    size_t lastSlash = savePath.rfind('/');
    if (lastSlash == std::string::npos) return false;
    std::string saveRoot = savePath.substr(0, lastSlash);

    const char* titles[] = {"524d4750", "524d4745", "524d474a", "524d474b"};
    const char* fileNames[] = {"GameData.bin", "save.bin"};
    FILE* in = nullptr;

    for (const char* t : titles) {
        for (const char* fn : fileNames) {
            std::string candidate = saveRoot + "/title/00010000/" + t + "/data/" + fn;
            in = fopen(candidate.c_str(), "rb");
            if (in) break;
        }
        if (in) break;
    }
    if (!in) {
        for (const char* fn : fileNames) {
            std::string candidate = saveRoot + "/" + fn;
            in = fopen(candidate.c_str(), "rb");
            if (in) break;
        }
    }
    if (!in) {
        std::string extCandidate = "/sdcard/GalaxyData/saves/GameData.bin";
        in = fopen(extCandidate.c_str(), "rb");
    }
    if (!in) {
        outToastMsg = "No save data found yet (play & save first)!";
        return false;
    }

    std::vector<uint8_t> data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        data.insert(data.end(), buf, buf + n);
    }
    fclose(in);

    mkdir("/sdcard/Download", 0770);
    std::string dstSave1 = "/sdcard/Download/GalaxyAndroid_save.bin";
    std::string dstSave2 = "/sdcard/Download/GameData.bin";
    std::string dstDolphin = "/sdcard/Download/GameData_Dolphin.bin";

    FILE* out1 = fopen(dstSave1.c_str(), "wb");
    FILE* out2 = fopen(dstSave2.c_str(), "wb");
    if (out1) { fwrite(data.data(), 1, data.size(), out1); fclose(out1); }
    if (out2) { fwrite(data.data(), 1, data.size(), out2); fclose(out2); }

    std::vector<uint8_t> dolphinData;
    if (DolphinSaveConverter::convertNativeToDolphin(data.data(), data.size(), dolphinData)) {
        FILE* outDol = fopen(dstDolphin.c_str(), "wb");
        if (outDol) {
            fwrite(dolphinData.data(), 1, dolphinData.size(), outDol);
            fclose(outDol);
        }
    }

    outToastMsg = "Exported to Download/ (includes Dolphin save)!";
    return true;
}

bool SaveSyncManager::importSave(const std::string& savePath, std::string& outToastMsg) {
    if (savePath.empty()) return false;
    size_t lastSlash = savePath.rfind('/');
    if (lastSlash == std::string::npos) return false;
    std::string saveRoot = savePath.substr(0, lastSlash);

    const char* downloadCandidates[] = {
        "/sdcard/Download/GalaxyAndroid_save.bin",
        "/sdcard/Download/GameData.bin",
        "/sdcard/Download/GameData_Dolphin.bin",
        "/sdcard/Download/save.bin",
        "/sdcard/dolphin-emu/Wii/title/00010000/524d4745/data/GameData.bin",
        "/sdcard/dolphin-emu/Wii/title/00010000/524d4750/data/GameData.bin",
        "/sdcard/dolphin-emu/Wii/title/00010000/524d474a/data/GameData.bin",
        "/sdcard/dolphin-emu/Wii/title/00010000/524d474b/data/GameData.bin",
        "/sdcard/GalaxyData/saves/GameData.bin",
        "/sdcard/GalaxyData/saves/GameData_Dolphin.bin"
    };

    FILE* in = nullptr;
    const char* matchedPath = nullptr;
    for (const char* path : downloadCandidates) {
        in = fopen(path, "rb");
        if (in) { matchedPath = path; break; }
    }
    if (!in) {
        outToastMsg = "Put GameData.bin in Download or Dolphin folder!";
        return false;
    }

    std::vector<uint8_t> data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        data.insert(data.end(), buf, buf + n);
    }
    fclose(in);

    if (data.empty()) {
        outToastMsg = "Save file in Downloads is empty!";
        return false;
    }

    bool wasDolphin = false;
    if (DolphinSaveConverter::isDolphinSave(data.data(), data.size())) {
        std::vector<uint8_t> nativeData;
        if (DolphinSaveConverter::convertDolphinToNative(data.data(), data.size(), nativeData)) {
            data = std::move(nativeData);
            wasDolphin = true;
        }
    }

    const char* titles[] = {"524d4750", "524d4745", "524d474a", "524d474b"};
    mkdir((saveRoot + "/title").c_str(), 0770);
    mkdir((saveRoot + "/title/00010000").c_str(), 0770);
    for (const char* t : titles) {
        std::string dir = saveRoot + "/title/00010000/" + t + "/data";
        mkdir((saveRoot + "/title/00010000/" + t).c_str(), 0770);
        mkdir(dir.c_str(), 0770);
        FILE* out = fopen((dir + "/GameData.bin").c_str(), "wb");
        if (out) { fwrite(data.data(), 1, data.size(), out); fclose(out); }
        FILE* outOld = fopen((dir + "/save.bin").c_str(), "wb");
        if (outOld) { fwrite(data.data(), 1, data.size(), outOld); fclose(outOld); }
    }

    mkdir("/sdcard/GalaxyData", 0770);
    mkdir("/sdcard/GalaxyData/saves", 0770);
    FILE* outExt = fopen("/sdcard/GalaxyData/saves/GameData.bin", "wb");
    if (outExt) {
        fwrite(data.data(), 1, data.size(), outExt);
        fclose(outExt);
    }

    outToastMsg = wasDolphin ? "Imported & converted Dolphin save successfully!" : "Imported save data successfully!";
    return true;
}

}  // namespace mobile_ui
