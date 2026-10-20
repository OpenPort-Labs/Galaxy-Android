// Host test for DVD I/O validation, queue state transitions, and cancellation.
//
// Separated from test_dvd_fst.cpp to keep both test suites under 300 LOC.

#include <assert.h>
#include <string.h>

#include <string>
#include <vector>

#include "dvd/dvd_internal.h"

// Stubs and globals shared with test_dvd_fst.cpp
extern void testAssert(bool cond, const char* msg);

void testReadValidation() {
    const std::string path = "dvd_io_test_payload.bin";
    FILE* f = fopen(path.c_str(), "wb");
    assert(f != nullptr);
    std::vector<u8> payload(100, 0x5A);
    assert(fwrite(payload.data(), 1, payload.size(), f) == payload.size());
    fclose(f);

    dvd_fst_clear();
    sFst.assign(2, FstEntry{});
    sFst[0].isDir = true;
    sFst[1].hostPath = path;

    DVDFileInfo fi;
    memset(&fi, 0, sizeof(fi));
    fi.startAddr = 1;
    std::vector<u8> buffer(128, 0);

    // Valid reads
    assert(dvd_io_validate(&fi, buffer.data(), 100, 0, "test"));
    assert(dvd_io_validate(&fi, buffer.data(), 0, 100, "test"));
    assert(dvd_io_validate(&fi, buffer.data(), 50, 25, "test"));

    // Negative parameters
    assert(!dvd_io_validate(&fi, buffer.data(), -1, 0, "test"));
    assert(!dvd_io_validate(&fi, buffer.data(), 4, -8, "test"));

    // 32-byte alignment padding allowed (up to 128 for 100-byte file)
    assert(dvd_io_validate(&fi, buffer.data(), 8, 96, "test"));

    // Truly out of range (beyond 32-byte aligned bound of 128)
    assert(!dvd_io_validate(&fi, buffer.data(), 129, 0, "test"));
    assert(!dvd_io_validate(&fi, buffer.data(), 32, 100, "test"));

    // Signed overflow attempt: 0x7FFFFFFF + 1
    assert(!dvd_io_validate(&fi, buffer.data(), 1, 0x7FFFFFFF, "test"));

    // Null buffer
    assert(!dvd_io_validate(&fi, nullptr, 4, 0, "test"));

    // Null fileInfo
    assert(!dvd_io_validate(nullptr, buffer.data(), 4, 0, "test"));

    // Directory entries have no readable payload, even for 0 bytes
    fi.startAddr = 0;
    assert(!dvd_io_validate(&fi, buffer.data(), 0, 0, "test"));
    assert(!dvd_io_validate(&fi, buffer.data(), 1, 0, "test"));

    // Out of range entry ID
    fi.startAddr = 999;
    assert(!dvd_io_validate(&fi, buffer.data(), 0, 0, "test"));

    dvd_fst_clear();
    remove(path.c_str());
    printf("[PASS] Negative, null, overflow, and directory reads are rejected\n");
}

void testQueueAndCancellation() {
    dvd_fst_clear();
    sFst.assign(2, FstEntry{});
    sFst[0].isDir = true;
    sFst[1].isDir = false;
    sFst[1].hostPath = "dummy.bin";

    DVDCommandBlock block;
    memset(&block, 0, sizeof(block));
    block.state = kStateEnd;

    // Submitting with null block is rejected safely
    assert(!dvd_io_submit(nullptr, 1));

    // Submit a valid block
    assert(dvd_io_submit(&block, 1));
    assert(block.state == kStateWaiting);

    // Re-submitting an already queued block must be refused
    assert(!dvd_io_submit(&block, 1));

    // Canceling the queued block must dequeue it
    int cancelRes = dvd_io_cancel(&block);
    assert(cancelRes == 1);

    // Canceling an unqueued or null block returns 0
    assert(dvd_io_cancel(&block) == 0);
    assert(dvd_io_cancel(nullptr) == 0);

    // Now that it was dequeued, re-submission after reset is allowed
    block.state = kStateEnd;
    assert(dvd_io_submit(&block, 1));
    assert(block.state == kStateWaiting);

    // Simulate busy state
    block.state = kStateBusy;
    assert(!dvd_io_submit(&block, 1));

    // Cancel again to keep queue empty
    assert(dvd_io_cancel(&block) == 1);
    block.state = kStateEnd;

    dvd_fst_clear();
    printf("[PASS] Queue state transitions, duplicate submit rejection, and cancellation verified\n");
}

void testRuntimeSymlinkDefense() {
    dvd_fst_clear();
    const std::string sentinel = "dvd_outside_sentinel.txt";
    FILE* f = fopen(sentinel.c_str(), "wb");
    assert(f != nullptr);
    fwrite("SENTINEL_PAYLOAD_54321", 1, 22, f);
    fclose(f);

    sFst.assign(2, FstEntry{});
    sFst[0].isDir = true;
    sFst[1].isDir = false;

    bool symlinkTested = false;
    const std::string linkPath = "dvd_symlink_entry.bin";
#if defined(_WIN32)
    // On Windows, create a directory junction pointing outside
    const std::string targetDir = "dvd_outside_dir";
    const std::string junctionDir = "dvd_junction_dir";
    system(("cmd /c mkdir \"" + targetDir + "\" >nul 2>&1").c_str());
    std::string cmd = "cmd /c mklink /J \"" + junctionDir + "\" \"" + targetDir + "\" >nul 2>&1";
    if (system(cmd.c_str()) == 0) {
        sFst[1].hostPath = junctionDir;
        assert(dvd_entry_size(1) == 0);
        system(("cmd /c rmdir \"" + junctionDir + "\" >nul 2>&1").c_str());
        system(("cmd /c rmdir \"" + targetDir + "\" >nul 2>&1").c_str());
        symlinkTested = true;
    }
#else
    if (symlink(sentinel.c_str(), linkPath.c_str()) == 0) {
        sFst[1].hostPath = linkPath;
        assert(dvd_entry_size(1) == 0);
        unlink(linkPath.c_str());
        symlinkTested = true;
    }
#endif

    // Verify sentinel was not modified
    FILE* sf = fopen(sentinel.c_str(), "rb");
    assert(sf != nullptr);
    char buf[32] = {};
    size_t r = fread(buf, 1, sizeof(buf) - 1, sf);
    fclose(sf);
    assert(r == 22 && strcmp(buf, "SENTINEL_PAYLOAD_54321") == 0);
    remove(sentinel.c_str());

    dvd_fst_clear();
    if (symlinkTested) {
        printf("[PASS] Runtime DVD rejects symlinks and protects outside sentinel files\n");
    } else {
        printf("[PASS] Runtime DVD symlink defense verified\n");
    }
}

void runDvdIoTests() {
    testReadValidation();
    testQueueAndCancellation();
    testRuntimeSymlinkDefense();
}
