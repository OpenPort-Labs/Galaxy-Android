#include "SaveSyncManager.h"
#include "DolphinSaveConverter.h"
#include "SaveStorage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifdef _WIN32
// Host-test portability; Android uses the POSIX calls directly.
#include <direct.h>
#define lstat stat
#define mkdir(path, mode) _mkdir(path)
#endif

namespace mobile_ui {
namespace {

constexpr size_t kMinSaveSize = 0x140;
constexpr size_t kMaxSaveSize = 0x10000;
const char* const kTitleIds[] = {"524d4750", "524d4745", "524d474a", "524d474b"};
const char* const kSaveNames[] = {"GameData.bin", "save.bin"};

std::string parentDir(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

bool makeDirs(const std::string& path) {
    if (path.empty()) return false;
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/') continue;
        std::string part = path.substr(0, i);
#ifdef _WIN32
        if (part.size() == 2 && part[1] == ':') continue;  // drive prefix, not a directory
#endif
        struct stat st;
        if (mkdir(part.c_str(), 0770) != 0 &&
            (errno != EEXIST || stat(part.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))) {
            return false;
        }
    }
    return true;
}

bool readSaveFile(const std::string& path, std::vector<uint8_t>& out) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (st.st_size < static_cast<off_t>(kMinSaveSize) ||
        st.st_size > static_cast<off_t>(kMaxSaveSize)) {
        return false;
    }
    FILE* in = fopen(path.c_str(), "rb");
    if (!in) return false;
    out.resize(static_cast<size_t>(st.st_size));
    bool ok = fread(out.data(), 1, out.size(), in) == out.size() && ferror(in) == 0;
    fclose(in);
    if (!ok) out.clear();
    return ok;
}

bool readFirstValid(const std::vector<std::string>& candidates, std::vector<uint8_t>& out) {
    for (const std::string& path : candidates) {
        if (readSaveFile(path, out)) return true;
    }
    return false;
}

std::vector<std::string> exportCandidates(const std::string& saveRoot, const SaveIoPaths& paths) {
    std::vector<std::string> candidates;
    for (const char* id : kTitleIds) {
        for (const char* name : kSaveNames) {
            candidates.push_back(saveRoot + "/title/00010000/" + id + "/data/" + name);
        }
    }
    for (const char* name : kSaveNames) candidates.push_back(saveRoot + "/" + name);
    candidates.push_back(paths.mirrorDir + "/GameData.bin");
    candidates.push_back(paths.mirrorDir + "/GameData_Dolphin.bin");
    return candidates;
}

std::vector<std::string> importCandidates(const SaveIoPaths& paths) {
    std::vector<std::string> candidates;
    for (const char* name : {"GalaxyAndroid_save.bin", "GameData.bin",
                             "GameData_Dolphin.bin", "save.bin"}) {
        candidates.push_back(paths.downloadDir + "/" + name);
    }
    for (const char* id : kTitleIds) {
        candidates.push_back(paths.dolphinDir + "/title/00010000/" + id + "/data/GameData.bin");
    }
    candidates.push_back(paths.mirrorDir + "/GameData.bin");
    candidates.push_back(paths.mirrorDir + "/GameData_Dolphin.bin");
    return candidates;
}

struct PromotedFile {
    std::string dest;
    std::string backup;
};

void rollback(std::vector<PromotedFile>& promoted) {
    for (auto it = promoted.rbegin(); it != promoted.rend(); ++it) {
        unlink(it->dest.c_str());
        if (!it->backup.empty()) rename(it->backup.c_str(), it->dest.c_str());
    }
    promoted.clear();
}

// All eight title copies are staged first; only when every write succeeded are
// they renamed into the live NAND tree. A failure during promotion restores the
// previous files, so an import can never leave a half-updated NAND.
bool writeNandSave(const std::string& saveRoot, const std::vector<uint8_t>& data) {
    std::string staging = saveRoot + "/import_staging";
    savefs::removeDirRecursive(staging);
    bool staged = makeDirs(staging + "/title/00010000");
    for (const char* id : kTitleIds) {
        std::string rel = std::string("title/00010000/") + id + "/data";
        staged = staged && makeDirs(staging + "/" + rel);
        for (const char* name : kSaveNames) {
            staged = staged && savefs::writeFileAtomic(staging + "/" + rel + "/" + name,
                                                       data.data(), data.size());
        }
    }
    if (!staged) {
        savefs::removeDirRecursive(staging);
        return false;
    }

    std::vector<PromotedFile> promoted;
    bool ok = true;
    for (const char* id : kTitleIds) {
        std::string destDir = saveRoot + "/title/00010000/" + id + "/data";
        if (!makeDirs(destDir)) {
            ok = false;
            break;
        }
        for (const char* name : kSaveNames) {
            std::string dest = destDir + "/" + name;
            std::string backup = dest + ".import_rollback";
            std::string stagedFile = staging + "/title/00010000/" + id + "/data/" + name;
            struct stat st;
            bool hadOld = lstat(dest.c_str(), &st) == 0;
            if (hadOld) {
                savefs::removeDirRecursive(backup);
                if (rename(dest.c_str(), backup.c_str()) != 0) {
                    ok = false;
                    break;
                }
            }
            if (rename(stagedFile.c_str(), dest.c_str()) != 0) {
                if (hadOld) rename(backup.c_str(), dest.c_str());
                ok = false;
                break;
            }
            promoted.push_back({dest, hadOld ? backup : std::string()});
        }
        if (!ok) break;
    }

    if (!ok) {
        rollback(promoted);
        savefs::removeDirRecursive(staging);
        return false;
    }
    for (const PromotedFile& file : promoted) {
        if (!file.backup.empty()) savefs::removeDirRecursive(file.backup);
    }
    savefs::removeDirRecursive(staging);
    return true;
}

}  // namespace

bool SaveSyncManager::exportSave(const std::string& savePath, std::string& outToastMsg,
                                 const SaveIoPaths& paths) {
    std::string saveRoot = parentDir(savePath);
    if (saveRoot.empty()) return false;

    std::vector<uint8_t> data;
    if (!readFirstValid(exportCandidates(saveRoot, paths), data)) {
        outToastMsg = "No save data found yet (play & save first)!";
        return false;
    }
    if (!makeDirs(paths.downloadDir)) {
        outToastMsg = "Could not create Download folder!";
        return false;
    }

    bool raw1 = savefs::writeFileAtomic(paths.downloadDir + "/GalaxyAndroid_save.bin",
                                        data.data(), data.size());
    bool raw2 = savefs::writeFileAtomic(paths.downloadDir + "/GameData.bin",
                                        data.data(), data.size());
    if (!raw1 || !raw2) {
        outToastMsg = "Export failed: could not write the save file!";
        return false;
    }

    std::vector<uint8_t> dolphinData;
    bool dolphinOk = DolphinSaveConverter::convertNativeToDolphin(data.data(), data.size(), dolphinData) &&
                     savefs::writeFileAtomic(paths.downloadDir + "/GameData_Dolphin.bin",
                                             dolphinData.data(), dolphinData.size());
    outToastMsg = dolphinOk ? "Exported to Download/ (includes Dolphin save)!"
                            : "Exported save to Download/ (Dolphin copy failed)!";
    return true;
}

bool SaveSyncManager::importSave(const std::string& savePath, std::string& outToastMsg,
                                 const SaveIoPaths& paths) {
    std::string saveRoot = parentDir(savePath);
    if (saveRoot.empty()) {
        outToastMsg = "Save path not set!";
        return false;
    }

    std::vector<uint8_t> data;
    bool foundAny = false;
    for (const std::string& path : importCandidates(paths)) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0) foundAny = true;
        if (readSaveFile(path, data)) break;
    }
    if (data.empty()) {
        outToastMsg = foundAny ? "Save file is too small, too large or unreadable!"
                               : "Put GameData.bin in Download or Dolphin folder!";
        return false;
    }

    bool wasDolphin = false;
    if (DolphinSaveConverter::isDolphinSave(data.data(), data.size())) {
        std::vector<uint8_t> nativeData;
        if (!DolphinSaveConverter::convertDolphinToNative(data.data(), data.size(), nativeData)) {
            outToastMsg = "Failed to convert Dolphin save (corrupt or invalid)!";
            return false;
        }
        data.swap(nativeData);
        wasDolphin = true;
    } else if (!DolphinSaveConverter::isNativeSave(data.data(), data.size())) {
        outToastMsg = "Save format unrecognized or corrupted!";
        return false;
    }

    if (!writeNandSave(saveRoot, data)) {
        outToastMsg = "Save import failed; previous NAND data restored!";
        return false;
    }

    bool mirrorOk = makeDirs(paths.mirrorDir) &&
                    savefs::writeFileAtomic(paths.mirrorDir + "/GameData.bin",
                                            data.data(), data.size());
    outToastMsg = wasDolphin ? "Imported & converted Dolphin save successfully!"
                             : "Imported save data successfully!";
    if (!mirrorOk) outToastMsg += " (external mirror failed)";
    return true;
}

}  // namespace mobile_ui
