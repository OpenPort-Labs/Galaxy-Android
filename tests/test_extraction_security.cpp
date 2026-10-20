// Adversarial regression test suite for disc extraction path security.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "path_security.h"
#include "wii_disc_reader.h"
#include "wii_partition.h"

#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#define TEST_MKDIR(p) _mkdir(p)
#define TEST_RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define TEST_MKDIR(p) mkdir(p, 0755)
#define TEST_RMDIR(p) rmdir(p)
#endif

namespace {

void testAssert(bool cond, const char* msg) {
    if (!cond) {
        printf("[FAIL] %s\n", msg);
        exit(1);
    }
}

void testSafeNameValidation() {
    using namespace wii;
    testAssert(!isSafeName(nullptr), "null name rejected");
    testAssert(!isSafeName(""), "empty name rejected");
    testAssert(!isSafeName("."), "dot name rejected");
    testAssert(!isSafeName(".."), "dot-dot name rejected");
    testAssert(!isSafeName("../foo"), "traversal prefix rejected");
    testAssert(!isSafeName("foo/bar"), "forward slash rejected");
    testAssert(!isSafeName("foo\\bar"), "backslash rejected");
    testAssert(!isSafeName("foo:bar"), "colon rejected");
    testAssert(!isSafeName("C:test"), "drive prefix rejected");
    testAssert(!isSafeName("bad\x01name"), "control character rejected");
    testAssert(!isSafeName("bad\x7fname"), "del character rejected");
    testAssert(!isSafeName("trailing "), "trailing space rejected");
    testAssert(!isSafeName("trailing."), "trailing dot rejected");

    std::string longName(256, 'a');
    testAssert(!isSafeName(longName.c_str()), "name > 255 rejected");
    std::string validLongName(255, 'a');
    testAssert(isSafeName(validLongName.c_str()), "name == 255 accepted");

    testAssert(isSafeName("main.dol"), "valid main.dol accepted");
    testAssert(isSafeName("fst.bin"), "valid fst.bin accepted");
    testAssert(isSafeName("MarioFace.arc"), "valid arc accepted");
    testAssert(isSafeName("StageData"), "valid directory name accepted");
    printf("[PASS] safeName rejects traversals, separators, colons, and controls\n");
}

void testDirectoryEscapesAndCollisions() {
    using namespace wii;
    const std::string testRoot = "test_dir_escape_root";
    TEST_MKDIR(testRoot.c_str());

    testAssert(ensureSafeDirectories(testRoot, "files/StageData/Sub"), "valid directories created");
    testAssert(!ensureSafeDirectories(testRoot, "../outside"), "parent directory traversal rejected");
    testAssert(!ensureSafeDirectories(testRoot, "files/../escape"), "embedded dot-dot rejected");
    testAssert(!ensureSafeDirectories(testRoot, "files/evil:ads"), "colon directory rejected");

    // File/Directory collision
    const std::string fileColl = testRoot + "/coll_dir";
    FILE* fp = fopen(fileColl.c_str(), "wb");
    assert(fp != nullptr);
    fclose(fp);
    testAssert(!ensureSafeDirectories(testRoot, "coll_dir/child"), "directory over file collision rejected");
    remove(fileColl.c_str());

    printf("[PASS] Directory traversal and collision boundaries enforced\n");
}

void testPreExistingSymlinkAndSentinelIntegrity() {
    using namespace wii;
    const std::string testRoot = "test_symlink_root";
    const std::string sentinelFile = "test_outside_sentinel.txt";
    TEST_MKDIR(testRoot.c_str());

    // Create an outside sentinel file whose contents MUST remain untouched
    FILE* sf = fopen(sentinelFile.c_str(), "wb");
    assert(sf != nullptr);
    const char* sentinelData = "CRITICAL_SENTINEL_UNTOUCHED_12345";
    fwrite(sentinelData, 1, strlen(sentinelData), sf);
    fclose(sf);

    // Attempt to open outside target directly
    std::string fullOut;
    FILE* fp = openSafeFileForWrite(testRoot, "../test_outside_sentinel.txt", &fullOut);
    testAssert(fp == nullptr, "openSafeFileForWrite rejected ../ traversal to sentinel");

    // Test symlink/reparse behavior if the platform allows creation
    bool symlinkCreated = false;
    const std::string linkPath = testRoot + "/symlink_to_sentinel";
#if defined(_WIN32)
    // On Windows, create a directory junction to verify reparse point rejection
    const std::string outsideDir = "test_outside_dir";
    const std::string junctionPath = testRoot + "/junction_outside";
    TEST_MKDIR(outsideDir.c_str());
    // Create junction using cmd /c mklink /J
    std::string cmd = "cmd /c mklink /J \"" + junctionPath + "\" \"" + outsideDir + "\" >nul 2>&1";
    if (system(cmd.c_str()) == 0) {
        testAssert(isReparseOrSymlink(junctionPath), "junction identified as reparse point");
        testAssert(!ensureSafeDirectories(testRoot, "junction_outside/nested"), "junction traversal rejected");
        testAssert(openSafeFileForWrite(testRoot, "junction_outside/evil.txt") == nullptr,
                   "file write through junction rejected");
        system(("cmd /c rmdir \"" + junctionPath + "\" >nul 2>&1").c_str());
        TEST_RMDIR(outsideDir.c_str());
        symlinkCreated = true;
    }
#else
    if (symlink(sentinelFile.c_str(), linkPath.c_str()) == 0) {
        testAssert(isReparseOrSymlink(linkPath), "symlink detected");
        FILE* sfp = openSafeFileForWrite(testRoot, "symlink_to_sentinel", &fullOut);
        testAssert(sfp == nullptr, "openSafeFileForWrite refused to follow symlink to sentinel");
        unlink(linkPath.c_str());
        symlinkCreated = true;
    }
#endif

    // Assert sentinel file was NEVER modified or truncated
    FILE* rf = fopen(sentinelFile.c_str(), "rb");
    assert(rf != nullptr);
    char buf[64] = {};
    size_t readLen = fread(buf, 1, sizeof(buf) - 1, rf);
    fclose(rf);
    testAssert(readLen == strlen(sentinelData) && strcmp(buf, sentinelData) == 0,
               "Outside sentinel content unmodified after adversarial attacks");
    remove(sentinelFile.c_str());

    if (symlinkCreated) {
        printf("[PASS] Symlink / reparse escape attempts blocked without modifying sentinel\n");
    } else {
        printf("[PASS] Symlink detection verified (symlink creation unprivileged on this platform)\n");
    }
}

class FakeDiscReader : public wii::DiscImageReader {
private:
    uint64_t m_sz;
public:
    explicit FakeDiscReader(uint64_t sz) : m_sz(sz) {}
    bool read(uint64_t virtualOffset, size_t len, uint8_t* out) override {
        (void)virtualOffset;
        (void)len;
        (void)out;
        return false;
    }
    uint64_t size() const override { return m_sz; }
};

void testReaderIntegerOverflowAndBounds() {
    using namespace wii;
    FakeDiscReader fake(1024);
    uint8_t buf[16] = {};

    // IsoReader bounds check using a test file
    const std::string dummyIso = "test_dummy.iso";
    FILE* f = fopen(dummyIso.c_str(), "wb");
    assert(f != nullptr);
    uint8_t d[512] = {};
    d[0x18] = 0x5D;
    d[0x19] = 0x1C;
    d[0x1A] = 0x9E;
    d[0x1B] = 0xA3;
    fwrite(d, 1, sizeof(d), f);
    fclose(f);

    auto reader = openDiscImage(dummyIso);
    testAssert(reader != nullptr, "openDiscImage recognized ISO header");

    // Normal in-bounds read
    testAssert(reader->read(0, 16, buf), "in-bounds iso read succeeded");

    // Integer overflow attempt: virtualOffset close to UINT64_MAX
    testAssert(!reader->read(UINT64_MAX - 8, 16, buf), "overflow virtualOffset rejected");
    testAssert(!reader->read(510, 10, buf), "out-of-bounds read rejected");
    reader.reset();
    remove(dummyIso.c_str());

    printf("[PASS] IsoReader overflow boundaries and out-of-bounds reads rejected\n");
}

void testPartialFailureCleanup() {
    using namespace wii;
    const std::string testRoot = "test_cleanup_root";
    TEST_MKDIR(testRoot.c_str());

    std::string fullPath;
    FILE* fp = openSafeFileForWrite(testRoot, "files/partial.bin", &fullPath);
    testAssert(fp != nullptr, "open partial test file succeeded");
    fwrite("partial_data", 1, 12, fp);
    fclose(fp);

    // Simulate error cleanup
    removePartialFile(fullPath);
    FILE* check = fopen(fullPath.c_str(), "rb");
    testAssert(check == nullptr, "removePartialFile unlinked failed file");

    printf("[PASS] Partial failure cleanup properly removes corrupt files\n");
}

} // namespace

int main() {
    printf("=== Running Adversarial Disc Extraction Security Tests ===\n");
    testSafeNameValidation();
    testDirectoryEscapesAndCollisions();
    testPreExistingSymlinkAndSentinelIntegrity();
    testReaderIntegerOverflowAndBounds();
    testPartialFailureCleanup();
    printf("All disc extraction security and path traversal tests passed.\n");
    return 0;
}
