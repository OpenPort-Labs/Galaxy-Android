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

    // Out of range
    assert(!dvd_io_validate(&fi, buffer.data(), 101, 0, "test"));
    assert(!dvd_io_validate(&fi, buffer.data(), 8, 96, "test"));

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

void runDvdIoTests() {
    testReadValidation();
    testQueueAndCancellation();
}
