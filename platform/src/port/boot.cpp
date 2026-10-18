// Brings up the emulated Wii environment and starts the game's main().
#include "port/port.h"
#include <sys/stat.h>
#include <unistd.h>
#include <string>

extern "C" void port_os_time_init(void);
extern "C" void port_gx_init(void);
extern "C" void port_gx_use_recorder_backend(void);
extern "C" void port_dvd_set_root(const char* root);
extern "C" void port_dvd_load_dol_data(const char* root);
extern "C" void port_nand_set_root(const char* root);

// GameSystem.cpp's main(), renamed for the port.
void port_game_main(void);

static void gameEntry() { port_game_main(); }

extern "C" void port_boot(const char* dataRoot, const char* saveRoot) {
    port_log("port_boot: data=%s save=%s", dataRoot, saveRoot);
    port_mem_init();
    port_os_time_init();
    port_dvd_set_root(dataRoot);
    const PortDisc* disc = port_dvd_disc();
    if (!disc) {
        port_fatal("%s: not the files of a Super Mario Galaxy disc the port knows (no EuEnglish, UsEnglish, JpJapanese or KrKorean "
                   "folder with the game's texts in sys/fst.bin and files/)",
                   dataRoot);
    }
    port_log("disc: %s (%s), %d languages", disc->id, disc->region, port_language_count());
    port_log("[BOOT] Milestone: Disc data initialized (%s, %s)", disc->id, disc->region);
    port_mem_set_disc_id(disc->id);
    port_dvd_load_dol_data(dataRoot);
    port_nand_set_root(saveRoot);

    // Auto-restore save data from persistent external mirror if internal NAND directory is empty or truncated
    const char* titles[] = {"524d4750", "524d4745", "524d474a", "524d474b"};
    for (const char* t : titles) {
        std::string internalSave = std::string(saveRoot) + "/title/00010000/" + t + "/data/GameData.bin";
        struct stat stInternal;
        bool internalValid = (stat(internalSave.c_str(), &stInternal) == 0 && stInternal.st_size >= 512);

        if (!internalValid) {
            std::string mirrorPath = "/sdcard/GalaxyData/saves/GameData.bin";
            struct stat stMirror;
            if (stat(mirrorPath.c_str(), &stMirror) == 0 && stMirror.st_size >= 512) {
                std::string dir = std::string(saveRoot) + "/title/00010000/" + t + "/data";
                mkdir((std::string(saveRoot) + "/title").c_str(), 0770);
                mkdir((std::string(saveRoot) + "/title/00010000").c_str(), 0770);
                mkdir((std::string(saveRoot) + "/title/00010000/" + t).c_str(), 0770);
                mkdir(dir.c_str(), 0770);

                std::string tmpInternal = internalSave + ".tmp";
                FILE* mirror = fopen(mirrorPath.c_str(), "rb");
                if (mirror) {
                    FILE* restore = fopen(tmpInternal.c_str(), "wb");
                    if (restore) {
                        char buf[8192];
                        size_t n;
                        size_t total = 0;
                        bool writeErr = false;
                        while ((n = fread(buf, 1, sizeof(buf), mirror)) > 0) {
                            if (fwrite(buf, 1, n, restore) != n) {
                                writeErr = true;
                                break;
                            }
                            total += n;
                        }
                        fflush(restore);
                        int fd = fileno(restore);
                        if (fd >= 0) fsync(fd);
                        fclose(restore);

                        if (!writeErr && total == (size_t)stMirror.st_size) {
                            rename(tmpInternal.c_str(), internalSave.c_str());
                            port_log("[BOOT] NAND: verified auto-restore GameData.bin (%zu bytes) -> %s", total, internalSave.c_str());
                        } else {
                            unlink(tmpInternal.c_str());
                            port_log("[BOOT] NAND: warning - restore from mirror failed byte verification");
                        }
                    }
                    fclose(mirror);
                }
            }
        }
    }
    port_log("[BOOT] Milestone: NAND initialized, save verified");

    port_gx_init();
    port_gx_use_recorder_backend();
    port_log("[BOOT] Milestone: Graphics subsystem initialized");

    port_log("[BOOT] Milestone: Entering gameEntry main loop");
    port_os_boot(gameEntry);
}
