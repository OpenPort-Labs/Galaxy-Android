// Adversarial tests for the Dolphin <-> native save converter's offset
// arithmetic and duplicate-extent handling.
#include "DolphinSaveConverter.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using mobile_ui::DolphinSaveConverter;

namespace {

int failures = 0;

void check(bool ok, const char* name, const std::string& detail = "") {
    printf("%-52s %s%s%s\n", name, ok ? "OK" : "FAIL",
           detail.empty() ? "" : " - ", detail.c_str());
    if (!ok) ++failures;
}

void putBE16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

void putBE32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

uint32_t getBE32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

constexpr uint32_t kSaveSize = 0x400;
constexpr uint32_t kRecordOff = 0x140;

// A GALA payload with one attachment and one galaxy entry. dsize covers the
// entry name plus one coin; the remaining coins only fit if the converter
// ignores the declared entry size.
std::vector<uint8_t> galaPayload() {
    std::vector<uint8_t> p(16, 0);
    putBE16(&p[0], 1);      // entry count
    putBE16(&p[2], 1);      // attachment count
    putBE16(&p[4], 6);      // entry size
    putBE16(&p[6], 0x1111); // attachment hash
    putBE16(&p[8], 0x2222); // attachment offset
    putBE16(&p[10], 0x3333); // galaxy name
    putBE16(&p[12], 0x4444); // coin 0
    putBE16(&p[14], 0x5555); // coin 1
    return p;
}

std::vector<uint8_t> makeDolphinSave(uint32_t recordOff, uint32_t recordOff2 = 0) {
    std::vector<uint8_t> save(kSaveSize, 0);
    save[7] = 2;   // big-endian version 2
    save[11] = 19; // 19 file records
    putBE32(&save[12], kSaveSize);
    for (uint32_t i = 0; i < 19; ++i) {
        putBE32(&save[16 + i * 16 + 12], i == 0 ? recordOff : (i == 1 ? recordOff2 : 0));
    }
    std::vector<uint8_t> payload = galaPayload();
    if (recordOff + 16ull + payload.size() <= save.size()) {
        uint8_t* rec = &save[recordOff];
        rec[1] = 1;  // one chunk follows the 4-byte file prefix
        uint8_t* chunk = rec + 4;
        memcpy(chunk, "GALA", 4);
        putBE32(chunk + 4, 0xAABBCCDDu);
        putBE32(chunk + 8, (uint32_t)(12 + payload.size()));
        memcpy(chunk + 12, payload.data(), payload.size());
    }
    putBE32(&save[0], DolphinSaveConverter::calcChecksum(save.data(), save.size(), true));
    return save;
}

void testRoundTripPreservesSave() {
    std::vector<uint8_t> dolphin = makeDolphinSave(kRecordOff);
    std::vector<uint8_t> native;
    bool ok = DolphinSaveConverter::convertDolphinToNative(dolphin.data(), dolphin.size(), native);
    check(ok, "valid Dolphin save converts");
    check(native.size() == dolphin.size(), "converted size is unchanged");
    check(native[4] == 2 && native[7] == 0 && native[8] == 19 && native[11] == 0,
          "converted header is native little-endian");
    check(memcmp(&native[kRecordOff + 4], "ALAG", 4) == 0, "chunk signature bytes swapped");
    check(getBE32(&native[kRecordOff + 12]) == 0x1C000000u, "chunk size is little-endian 28");

    std::vector<uint8_t> back;
    bool okBack = DolphinSaveConverter::convertNativeToDolphin(native.data(), native.size(), back);
    check(okBack, "native save converts back");
    check(back == dolphin, "round trip reproduces the original Dolphin save");
}

void testChunkSizePastEndRejected() {
    std::vector<uint8_t> save = makeDolphinSave(kRecordOff);
    putBE32(&save[kRecordOff + 12], 0xFFFFFFF0u);
    std::vector<uint8_t> out;
    check(!DolphinSaveConverter::convertDolphinToNative(save.data(), save.size(), out),
          "chunk size beyond the buffer rejected");
}

void testRecordOffsetPastEndIgnored() {
    std::vector<uint8_t> save = makeDolphinSave(0xFFFFFFFCu);
    std::vector<uint8_t> out;
    bool ok = DolphinSaveConverter::convertDolphinToNative(save.data(), save.size(), out);
    check(ok, "record offset past the buffer does not crash");
    check(out.size() == save.size(), "output still contains the whole input");
}

void testRecordOffsetInsideHeaderIgnored() {
    std::vector<uint8_t> save = makeDolphinSave(kRecordOff, 0x20u);
    // Put a plausible chunk header in the file-record table; it must not be
    // byte-swapped as if it were file data.
    memcpy(&save[0x20], "GALA", 4);
    putBE32(&save[0x24], 0x11223344u);
    putBE32(&save[0x28], 16u);
    putBE32(&save[0], DolphinSaveConverter::calcChecksum(save.data(), save.size(), true));
    std::vector<uint8_t> out;
    bool ok = DolphinSaveConverter::convertDolphinToNative(save.data(), save.size(), out);
    check(ok, "record offset inside the header is ignored");
    check(memcmp(&out[0x20], "GALA", 4) == 0, "header bytes are left untouched");
}

void testDuplicateRecordRejected() {
    std::vector<uint8_t> save = makeDolphinSave(kRecordOff, kRecordOff);
    std::vector<uint8_t> out;
    check(!DolphinSaveConverter::convertDolphinToNative(save.data(), save.size(), out),
          "two records sharing a chunk extent rejected");
}

void testWrongHeaderRejected() {
    std::vector<uint8_t> save = makeDolphinSave(kRecordOff);
    save[11] = 18;
    std::vector<uint8_t> out;
    check(!DolphinSaveConverter::convertDolphinToNative(save.data(), save.size(), out),
          "wrong file count rejected");

    std::vector<uint8_t> oversized(0x10001, 0);
    oversized[7] = 2;
    oversized[11] = 19;
    putBE32(&oversized[12], kSaveSize);
    check(!DolphinSaveConverter::convertDolphinToNative(oversized.data(), oversized.size(), out),
          "input above the format size cap rejected");
}

void testHugeChunkCountersAreBounded() {
    std::vector<uint8_t> save = makeDolphinSave(kRecordOff);
    uint8_t* chunk = &save[kRecordOff + 4];
    putBE32(chunk + 8, 12 + 6);
    putBE16(chunk + 12, 0xFFFF);  // entry count
    putBE16(chunk + 14, 0xFFFF);  // attachment count
    putBE16(chunk + 16, 0xFFFF);  // entry stride
    std::vector<uint8_t> out;
    bool ok = DolphinSaveConverter::convertDolphinToNative(save.data(), save.size(), out);
    check(ok, "oversized GALA counters terminate without writing out of bounds");
}

}  // namespace

int main() {
    printf("=== Running Dolphin Save Converter Tests ===\n");
    testRoundTripPreservesSave();
    testChunkSizePastEndRejected();
    testRecordOffsetPastEndIgnored();
    testRecordOffsetInsideHeaderIgnored();
    testDuplicateRecordRejected();
    testWrongHeaderRejected();
    testHugeChunkCountersAreBounded();
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
