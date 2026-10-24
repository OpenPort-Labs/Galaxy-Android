// Save import/export integrity: staged NAND writes, rollback on failure and
// honest error reporting. Uses the real SaveSyncManager against temp dirs.
#include "DolphinSaveConverter.h"
#include "SaveStorage.h"
#include "SaveSyncManager.h"

#include <stdio.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define TEST_GETPID _getpid
#else
#include <unistd.h>
#define TEST_GETPID getpid
#endif

namespace fs = std::filesystem;
using mobile_ui::SaveIoPaths;
using mobile_ui::SaveSyncManager;
using mobile_ui::savefs::validateSaveDir;

namespace {

int failures = 0;

void check(const char* name, bool ok, const std::string& detail = "") {
    printf("%-56s %s%s%s\n", name, ok ? "OK" : "FAIL",
           detail.empty() ? "" : " - ", detail.c_str());
    if (!ok) ++failures;
}

std::vector<uint8_t> nativeSave(uint8_t tag) {
    std::vector<uint8_t> data(0x400, 0);
    data[4] = 2;   // native layout: little-endian version 2
    data[8] = 19;  // 19 files
    data[12] = 0x00;
    data[13] = 0x04;
    data[0x100] = tag;
    return data;
}

std::vector<uint8_t> dolphinSave() {
    std::vector<uint8_t> data(0x400, 0);
    data[7] = 2;   // big-endian version 2
    data[11] = 19; // 19 files
    data[14] = 4;
    return data;
}

bool writeFile(const fs::path& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return out.good();
}

std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

const char* const kTitles[] = {"524d4750", "524d4745", "524d474a", "524d474b"};
const char* const kNames[] = {"GameData.bin", "save.bin"};

struct Fixture {
    fs::path root;
    fs::path nand;
    fs::path download;
    fs::path dolphin;
    fs::path mirror;
    SaveIoPaths paths;
    std::string savePath;

    Fixture() {
        root = fs::temp_directory_path() /
               ("galaxy_save_sync_" + std::to_string(TEST_GETPID()) + "_" +
                std::to_string(static_cast<unsigned long long>(::time(nullptr))));
        fs::remove_all(root);
        nand = root / "nand";
        download = root / "Download";
        dolphin = root / "dolphin";
        mirror = root / "mirror";
        fs::create_directories(nand);
        paths.downloadDir = download.generic_string();
        paths.dolphinDir = dolphin.generic_string();
        paths.mirrorDir = mirror.generic_string();
        savePath = (nand / "touch_settings.ini").generic_string();
    }

    ~Fixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path titleFile(const char* id, const char* name) const {
        return nand / "title" / "00010000" / id / "data" / name;
    }

    bool hasLeftover(const std::string& marker) const {
        for (const auto& entry : fs::recursive_directory_iterator(nand)) {
            if (entry.path().filename().string().find(marker) != std::string::npos) return true;
        }
        return false;
    }
};

void testImportWritesEveryCopy() {
    Fixture fixture;
    const std::vector<uint8_t> sample = nativeSave(0x5a);
    fs::create_directories(fixture.download);
    writeFile(fixture.download / "GameData.bin", sample);

    std::string message;
    bool ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("import succeeds", ok, message);
    for (const char* id : kTitles) {
        for (const char* name : kNames) {
            check((std::string("imported ") + id + "/" + name).c_str(),
                  readFile(fixture.titleFile(id, name)) == sample);
        }
    }
    check("imported NAND validates", validateSaveDir(fixture.nand.generic_string()));
    check("import mirrors externally", readFile(fixture.mirror / "GameData.bin") == sample);
    check("import staging removed", !fs::exists(fixture.nand / "import_staging"));
}

void testImportRollsBackOnPromotionFailure() {
    Fixture fixture;
    const std::vector<uint8_t> previous = nativeSave(0x11);
    fs::create_directories(fixture.titleFile("524d4750", "GameData.bin").parent_path());
    writeFile(fixture.titleFile("524d4750", "GameData.bin"), previous);
    // A regular file where the next title's directory must be created makes
    // promotion fail after the first title was already replaced.
    writeFile(fixture.nand / "title" / "00010000" / "524d4745", nativeSave(0x22));

    fs::create_directories(fixture.download);
    writeFile(fixture.download / "GameData.bin", nativeSave(0x5a));
    std::string message;
    bool ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("import refuses blocked NAND", !ok, message);
    check("rollback restores previous save",
          readFile(fixture.titleFile("524d4750", "GameData.bin")) == previous);
    check("rollback removes partially promoted file",
          !fs::exists(fixture.titleFile("524d4750", "save.bin")));
    check("rollback leaves no staging", !fs::exists(fixture.nand / "import_staging"));
    check("rollback leaves no backups", !fixture.hasLeftover(".import_rollback"));
    check("failed import does not mirror", !fs::exists(fixture.mirror / "GameData.bin"));
}

void testImportRejectsOversizedFile() {
    Fixture fixture;
    fs::create_directories(fixture.download);
    writeFile(fixture.download / "GameData.bin", std::vector<uint8_t>(0x10001, 0));
    std::string message;
    bool ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("oversized import rejected", !ok);
    check("oversized import explains size", message.find("too large") != std::string::npos, message);
    check("oversized import writes nothing", !fs::exists(fixture.nand / "title"));
}

void testImportConvertsDolphinSave() {
    Fixture fixture;
    fs::create_directories(fixture.download);
    writeFile(fixture.download / "GameData_Dolphin.bin", dolphinSave());
    std::string message;
    bool ok = SaveSyncManager::importSave(fixture.savePath, message, fixture.paths);
    check("Dolphin import succeeds", ok, message);
    const std::vector<uint8_t> stored = readFile(fixture.titleFile("524d4750", "GameData.bin"));
    check("Dolphin import converts to native layout",
          stored.size() == 0x400 && stored[4] == 2 && stored[7] == 0 && stored[8] == 19);
    check("Dolphin import reports conversion", message.find("Dolphin") != std::string::npos, message);
}

void testExportWritesVerifiedCopies() {
    Fixture fixture;
    const std::vector<uint8_t> sample = nativeSave(0x77);
    fs::create_directories(fixture.titleFile("524d4750", "GameData.bin").parent_path());
    writeFile(fixture.titleFile("524d4750", "GameData.bin"), sample);

    std::string message;
    bool ok = SaveSyncManager::exportSave(fixture.savePath, message, fixture.paths);
    check("export succeeds", ok, message);
    check("export writes GalaxyAndroid_save.bin",
          readFile(fixture.download / "GalaxyAndroid_save.bin") == sample);
    check("export writes GameData.bin", readFile(fixture.download / "GameData.bin") == sample);

    std::vector<uint8_t> converted;
    mobile_ui::DolphinSaveConverter::convertNativeToDolphin(
        sample.data(), sample.size(), converted);
    check("export writes Dolphin save",
          readFile(fixture.download / "GameData_Dolphin.bin") == converted);
}

void testExportReportsWriteFailure() {
    Fixture fixture;
    const std::vector<uint8_t> sample = nativeSave(0x77);
    fs::create_directories(fixture.titleFile("524d4750", "GameData.bin").parent_path());
    writeFile(fixture.titleFile("524d4750", "GameData.bin"), sample);
    // A file where the download directory belongs forces every write to fail.
    fixture.paths.downloadDir = (fixture.root / "blocked").generic_string();
    writeFile(fixture.root / "blocked", sample);

    std::string message;
    bool ok = SaveSyncManager::exportSave(fixture.savePath, message, fixture.paths);
    check("export reports unwritable destination", !ok, message);
    check("export failure is explicit", message.find("failed") != std::string::npos ||
                                             message.find("Could not") != std::string::npos,
          message);
}

}  // namespace

int main() {
    testImportWritesEveryCopy();
    testImportRollsBackOnPromotionFailure();
    testImportRejectsOversizedFile();
    testImportConvertsDolphinSave();
    testExportWritesVerifiedCopies();
    testExportReportsWriteFailure();

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
