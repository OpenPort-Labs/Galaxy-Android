// Adversarial tests for declared FST/DOL sizes and the extraction output quota.
// Uses a synthetic encrypted Wii disc; no proprietary assets are involved.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "wii_extractor.h"
#include "wii_synthetic_disc.h"

using wii_test::FileSpec;
using wii_test::SyntheticWiiDisc;

namespace {

int failures = 0;

void check(bool ok, const char* name, const std::string& detail = "") {
    printf("%-52s %s%s%s\n", name, ok ? "OK" : "FAIL",
           detail.empty() ? "" : " - ", detail.c_str());
    if (!ok) ++failures;
}

std::string tempRoot(const char* tag) {
    return std::string("test_wii_limits_") + tag;
}

struct Result {
    bool ok = false;
    int lastPercent = -1;
    std::vector<std::string> logs;
};

bool writeBytes(const std::string& path, const std::vector<uint8_t>& data, size_t limit) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const size_t n = limit < data.size() ? limit : data.size();
    out.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)n);
    return out.good();
}

Result runExtraction(const SyntheticWiiDisc& spec, const std::string& root, size_t imageLimit = SIZE_MAX) {
    Result r;
    std::vector<uint8_t> image = spec.build();
    const std::string imagePath = root + ".iso";
    const std::string outDir = root + "_out";
    std::error_code ec;
    std::filesystem::remove_all(outDir, ec);
    if (!writeBytes(imagePath, image, imageLimit)) {
        r.logs.push_back("[test] failed to write image");
        return r;
    }
    r.ok = wii::extractWiiDisc(
        imagePath, outDir, true,
        [&r](const std::string& m) { r.logs.push_back(m); },
        [&r](int percent, const std::string&) { r.lastPercent = percent; });
    return r;
}

bool hasLog(const Result& r, const char* needle) {
    for (const std::string& line : r.logs) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

bool fileMatches(const std::string& path, char fill, size_t expectedSize) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() != expectedSize) return false;
    for (char c : data) {
        if (c != fill) return false;
    }
    return true;
}

bool exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

void cleanup(const std::string& root) {
    std::error_code ec;
    std::filesystem::remove(root + ".iso", ec);
    std::filesystem::remove_all(root + "_out", ec);
}

void testValidDiscExtracts() {
    const std::string root = tempRoot("valid");
    SyntheticWiiDisc disc;
    disc.files = {{"a.bin", 0x1000, 0x40}, {"b.bin", 0x1100, 0x80}};
    Result r = runExtraction(disc, root);
    check(r.ok, "valid synthetic disc extracts", hasLog(r, "[Error]") ? r.logs.back() : "");
    check(r.lastPercent == 100, "progress reaches 100");
    const std::string out = root + "_out";
    check(exists(out + "/sys/main.dol"), "sys/main.dol written");
    check(exists(out + "/sys/fst.bin"), "sys/fst.bin written");
    check(fileMatches(out + "/files/a.bin", 'A', 0x40), "a.bin content intact");
    check(fileMatches(out + "/files/b.bin", 'B', 0x80), "b.bin content intact");
    cleanup(root);
}

void testFstSizeOverflowRejected() {
    const std::string root = tempRoot("fstsize");
    SyntheticWiiDisc disc;
    disc.files = {{"a.bin", 0x1000, 0x40}};
    disc.fstSizeField = 0xFFFFFFFFu;  // << 2 must not wrap into a plausible size
    Result r = runExtraction(disc, root);
    check(!r.ok, "overflowing FST size rejected");
    check(hasLog(r, "FST"), "FST rejection is explained");
    check(!exists(root + "_out/sys/main.dol"), "rejection happens before extraction");
    cleanup(root);
}

void testFstEntryCountRejected() {
    const std::string root = tempRoot("entries");
    SyntheticWiiDisc disc;
    disc.files = {{"a.bin", 0x1000, 0x40}};
    disc.fstEntryCount = 0x40000000u;
    Result r = runExtraction(disc, root);
    check(!r.ok, "implausible FST entry count rejected");
    check(hasLog(r, "FST entry count"), "entry-count rejection is explained");
    cleanup(root);
}

void testDolSectionOverflowRejected() {
    const std::string root = tempRoot("dol");
    SyntheticWiiDisc disc;
    disc.files = {{"a.bin", 0x1000, 0x40}};
    disc.dolSectionOff = 0x100;
    disc.dolSectionLen = 0xFFFFFF80u;  // in 32-bit: off+len wraps to 0x80
    Result r = runExtraction(disc, root);
    check(!r.ok, "overflowing DOL section rejected");
    check(hasLog(r, "DOL"), "DOL rejection is explained");
    cleanup(root);
}

void testOutputQuotaRejected() {
    const std::string root = tempRoot("quota");
    SyntheticWiiDisc disc;
    for (int i = 0; i < 8; ++i) {
        disc.files.push_back({"d" + std::to_string(i) + ".bin", 0x1000, 0x4000});
    }
    // Every entry is individually in range, but together they declare twice
    // the partition size, so a repeated extent must not be written 8 times.
    Result r = runExtraction(disc, root);
    check(!r.ok, "repeated extents exceeding the output quota rejected");
    check(hasLog(r, "partition"), "quota rejection is explained");
    check(!exists(root + "_out/files/d0.bin"), "quota rejection leaves no file output");
    check(!exists(root + "_out/sys/main.dol"), "quota rejection leaves no system output");
    cleanup(root);
}

void testOutOfRangeFileIsSkipped() {
    const std::string root = tempRoot("oor");
    SyntheticWiiDisc disc;
    disc.files = {{"far.bin", 0x1F000, 0x10}, {"a.bin", 0x1000, 0x40}};
    Result r = runExtraction(disc, root);
    check(r.ok, "out-of-range file entry does not abort extraction");
    check(!exists(root + "_out/files/far.bin"), "out-of-range file is not written");
    check(fileMatches(root + "_out/files/a.bin", 'B', 0x40), "in-range file still written");
    cleanup(root);
}

void testTruncatedImageRejected() {
    const std::string root = tempRoot("trunc");
    SyntheticWiiDisc disc;
    disc.files = {{"a.bin", 0x1000, 0x40}};
    Result r = runExtraction(disc, root, 0x300);
    check(!r.ok, "truncated disc image rejected");
    cleanup(root);
}

}  // namespace

int main() {
    printf("=== Running Wii Disc Extraction Limit Tests ===\n");
    testValidDiscExtracts();
    testFstSizeOverflowRejected();
    testFstEntryCountRejected();
    testDolSectionOverflowRejected();
    testOutputQuotaRejected();
    testOutOfRangeFileIsSkipped();
    testTruncatedImageRejected();
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
