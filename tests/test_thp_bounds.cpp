// Bounds tests for the platform THP video decoder.
//
// The decoder consumes movie data taken from a user-supplied disc image, so
// every length, table and frame dimension in the stream is untrusted.  These
// cases exercise the parser with truncated and impossible segments, then build
// one complete 16x16 frame to prove the hardened paths still decode real data.
#include "thp_types.h"

#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include <vector>

extern "C" void THPSetInputSize(u32 size);
extern "C" void THPSetOutputSize(u32 width, u32 height);
extern "C" s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV, void* work);

namespace {

int failures = 0;

void check(const char* name, s32 got, s32 want) {
    bool ok = got == want;
    printf("%-45s got=%d want=%d %s\n", name, got, want, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

void checkError(const char* name, const std::vector<u8>& input, u32 outW, u32 outH, s32 want) {
    u8 y[640 * 480], uv[320 * 240];
    memset(y, 0, sizeof(y));
    memset(uv, 0, sizeof(uv));
    u32 work[16] = {0};
    THPSetInputSize((u32)input.size());
    THPSetOutputSize(outW, outH);
    check(name, THPVideoDecode((void*)input.data(), y, uv, uv, work), want);
}

void append(std::vector<u8>& out, std::initializer_list<u8> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

std::vector<u8> minimFrame(s32 acValue = 0x00) {
    std::vector<u8> f;
    append(f, {0xFF, 0xD8});

    append(f, {0xFF, 0xDB, 0x00, 0x43, 0x00});
    f.resize(f.size() + 64, 1);

    append(f, {0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01});
    f.resize(f.size() + 15, 0);
    append(f, {0x00});

    append(f, {0xFF, 0xC4, 0x00, 0x14, 0x10, 0x01});
    f.resize(f.size() + 15, 0);
    append(f, {(u8)acValue});

    append(f, {0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00, 0x10, 0x00, 0x10, 0x03,
               0x01, 0x22, 0x00, 0x02, 0x11, 0x00, 0x03, 0x11, 0x00});

    append(f, {0xFF, 0xDA, 0x00, 0x0C, 0x03,
               0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x3F, 0x00});
    append(f, {0x00, 0x00, 0x00});
    return f;
}

void testValidFrame() {
    std::vector<u8> frame = minimFrame();
    u8 y[16 * 16], uv[8 * 8];
    memset(y, 0xAA, sizeof(y));
    memset(uv, 0xAA, sizeof(uv));
    u32 work[16] = {0};
    THPSetInputSize((u32)frame.size());
    THPSetOutputSize(16, 16);
    check("valid 16x16 frame", THPVideoDecode((void*)frame.data(), y, uv, uv, work), 0);
    bool flat = true;
    for (u8 v : y) flat = flat && v == 128;
    for (u8 v : uv) flat = flat && v == 128;
    check("valid frame decodes to flat grey", flat ? 0 : -1, 0);
}

}  // namespace

int main() {
    testValidFrame();

    checkError("truncated DQT length", {0xFF, 0xDB, 0x00, 0xFF}, 0, 0, thp::kErrBadSyntax);
    checkError("DQT payload past end", {0xFF, 0xDB, 0x00, 0x04, 0x00}, 0, 0, thp::kErrBadSyntax);
    std::vector<u8> badPq = {0xFF, 0xDB, 0x00, 0x43, 0x20};
    badPq.resize(69, 0);
    checkError("DQT pq>1", badPq, 0, 0, thp::kErrBadTable);

    std::vector<u8> sof = {0xFF, 0xC0, 0x00, 0x11, 0x08, 0x01, 0xE0, 0x02, 0x80, 0x03,
                           0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01};
    checkError("SOF 640x480 vs 320x240 output", sof, 320, 240, thp::kErrBadSyntax);
    std::vector<u8> huge = {0xFF, 0xC0, 0x00, 0x11, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0x03,
                            0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01};
    checkError("SOF absurd dimensions", huge, 0, 0, thp::kErrBadSyntax);

    std::vector<u8> dht = {0xFF, 0xC4, 0x00, 0x13, 0x00};
    dht.resize(dht.size() + 16, 0xFF);
    checkError("DHT counts overflow", dht, 0, 0, thp::kErrBadTable);
    checkError("DHT truncated counts", {0xFF, 0xC4, 0x00, 0x13, 0x00, 0x01}, 0, 0, thp::kErrBadSyntax);
    checkError("APP segment length past end", {0xFF, 0xE0, 0x00, 0x50, 0x00}, 0, 0, thp::kErrBadSyntax);
    checkError("truncated restart interval", {0xFF, 0xDD, 0x00, 0x04, 0x00}, 0, 0, thp::kErrBadSyntax);

    std::vector<u8> beyond = {0xFF, 0xD8, 0xFF, 0xD8, 0xFF, 0xD9};
    u8 out[64] = {0};
    u32 work[16] = {0};
    THPSetInputSize(4);
    check("declared input size honoured", THPVideoDecode((void*)beyond.data(), out, out, out, work),
          thp::kErrBadSyntax);
    THPSetInputSize(0);
    check("zero input size rejected", THPVideoDecode((void*)beyond.data(), out, out, out, work),
          thp::kErrNoInput);

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
