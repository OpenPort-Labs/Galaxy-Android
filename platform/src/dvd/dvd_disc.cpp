#include "dvd_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "port/heap_routing.h"
#include "revolution/sc.h"

u32 dvd_be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

std::vector<u8> dvd_read_fst(const std::string& root) {
    std::vector<u8> data;
    FILE* f = fopen((root + "/sys/fst.bin").c_str(), "rb");
    if (!f) {
        return data;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len >= 12) {
        data.resize((size_t)len);
        if (fread(data.data(), 1, data.size(), f) != data.size() || (uint64_t)dvd_be32(&data[8]) * 12 >= data.size()) {
            data.clear();
        }
    }
    fclose(f);
    return data;
}

bool dvd_fst_has_root_dir(const std::vector<u8>& fst, const char* name) {
    if (fst.size() < 12) {
        return false;
    }
    u32 count = dvd_be32(&fst[8]);
    if (count < 1 || (size_t)count > fst.size() / 12) {
        return false;
    }
    const char* strings = (const char*)&fst[(size_t)count * 12];
    size_t stringsSize = fst.size() - (size_t)count * 12;
    size_t len = strlen(name);
    for (u32 i = 1; i < count;) {
        const u8* e = &fst[i * 12];
        bool isDir = e[0] != 0;
        u32 nameOff = dvd_be32(e) & 0x00FFFFFF;
        if (isDir && (size_t)nameOff + len < stringsSize && strncasecmp(strings + nameOff, name, len + 1) == 0) {
            return true;
        }
        u32 next = dvd_be32(e + 8);
        i = (isDir && next > i) ? next : i + 1;
    }
    return false;
}

static const PortDisc kDiscs[] = {
    {"RMGP01",
     "Europe",
     {{"english", "EuEnglish", SC_LANG_ENGLISH},
      {"french", "EuFrench", SC_LANG_FRENCH},
      {"german", "EuGerman", SC_LANG_GERMAN},
      {"spanish", "EuSpanish", SC_LANG_SPANISH},
      {"italian", "EuItalian", SC_LANG_ITALIAN}},
     5},
    {"RMGE01",
     "North America",
     {{"english", "UsEnglish", SC_LANG_ENGLISH}, {"french", "UsFrench", SC_LANG_FRENCH}, {"spanish", "UsSpanish", SC_LANG_SPANISH}},
     3},
    {"RMGJ01", "Japan", {{"japanese", "JpJapanese", SC_LANG_JAPANESE}}, 1},
    {"RMGK01", "Korea", {{"korean", "KrKorean", SC_LANG_KOREAN}}, 1},
};
static const PortDisc* sDisc = nullptr;
static const PortLanguage* sLanguages[PORT_DISC_MAX_LANGUAGES];
static int sLanguageCount = 0;
static char sLanguageWanted[16];
static const PortLanguage* sLanguageRunning = nullptr;

static bool hasTexts(const std::string& root, const std::vector<u8>& fst, const PortLanguage& language) {
    std::string texts = root + "/files/" + language.folder + "/MessageData/Message.arc";
    struct stat st;
    return dvd_fst_has_root_dir(fst, language.folder) && stat(texts.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

extern "C" const PortDisc* port_dvd_identify(const char* root) {
    PortHostAllocScope scope;
    std::vector<u8> fst = dvd_read_fst(root);
    if (fst.empty()) {
        return nullptr;
    }
    for (const PortDisc& disc : kDiscs) {
        if (hasTexts(root, fst, disc.languages[0])) {
            return &disc;
        }
    }
    return nullptr;
}

extern "C" const PortDisc* port_dvd_disc(void) { return sDisc; }

extern "C" void port_dvd_set_root(const char* root) {
    PortHostAllocScope scope;
    sDataRoot = root;
    sDisc = port_dvd_identify(root);
    sLanguageCount = 0;
    if (sDisc) {
        std::vector<u8> fst = dvd_read_fst(root);
        for (int i = 0; i < sDisc->languageCount; i++) {
            if (i == 0 || hasTexts(root, fst, sDisc->languages[i])) {
                sLanguages[sLanguageCount++] = &sDisc->languages[i];
            }
        }
    }
    if (const char* name = getenv("PETARI_LANGUAGE")) {
        port_language_set(name);
    }
}

extern "C" int port_language_count(void) { return sLanguageCount; }
extern "C" const char* port_language_name(int index) { return index >= 0 && index < sLanguageCount ? sLanguages[index]->name : ""; }

extern "C" void port_language_set(const char* name) {
    size_t n = 0;
    for (; name && name[n] && n + 1 < sizeof(sLanguageWanted); n++) {
        sLanguageWanted[n] = (char)tolower((unsigned char)name[n]);
    }
    sLanguageWanted[n] = 0;
}

extern "C" int port_language_wanted(void) {
    if (sLanguageWanted[0] == 0) {
        return 0;
    }
    for (int i = 0; i < sLanguageCount; i++) {
        if (strcmp(sLanguageWanted, sLanguages[i]->name) == 0) {
            return i;
        }
    }
    for (int i = 0; i < sLanguageCount; i++) {
        const char* lang = sLanguages[i]->name;
        if (strncmp(sLanguageWanted, lang, strlen(sLanguageWanted)) == 0) return i;
        if ((strcmp(sLanguageWanted, "es") == 0 || strcmp(sLanguageWanted, "spa") == 0) && strcmp(lang, "spanish") == 0) return i;
        if ((strcmp(sLanguageWanted, "fr") == 0 || strcmp(sLanguageWanted, "fra") == 0) && strcmp(lang, "french") == 0) return i;
        if ((strcmp(sLanguageWanted, "de") == 0 || strcmp(lang, "ger") == 0) && strcmp(lang, "german") == 0) return i;
        if ((strcmp(sLanguageWanted, "it") == 0 || strcmp(sLanguageWanted, "ita") == 0) && strcmp(lang, "italian") == 0) return i;
        if ((strcmp(sLanguageWanted, "en") == 0 || strcmp(sLanguageWanted, "eng") == 0) && strcmp(lang, "english") == 0) return i;
        if ((strcmp(sLanguageWanted, "ja") == 0 || strcmp(sLanguageWanted, "jpn") == 0) && strcmp(lang, "japanese") == 0) return i;
        if ((strcmp(sLanguageWanted, "ko") == 0 || strcmp(sLanguageWanted, "kor") == 0) && strcmp(lang, "korean") == 0) return i;
    }
    return 0;
}

extern "C" int port_language_running(void) {
    for (int i = 0; i < sLanguageCount; i++) {
        if (sLanguages[i] == sLanguageRunning) {
            return i;
        }
    }
    return -1;
}

extern "C" int port_language_start(void) {
    if (sLanguageCount == 0) {
        return SC_LANG_ENGLISH;
    }
    const PortLanguage* language = sLanguages[port_language_wanted()];
    if (language != sLanguageRunning) {
        sLanguageRunning = language;
        port_log("language: %s (texts from %s)", language->name, language->folder);
    }
    return language->code;
}

extern "C" const char* port_dvd_root(void) { return sDataRoot.c_str(); }
