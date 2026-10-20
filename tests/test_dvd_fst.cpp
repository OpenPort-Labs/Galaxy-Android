// Host test for the DVD layer's untrusted-input handling.
//
// Builds production dvd_fst.cpp / dvd_read.cpp against stubs for host allocation,
// logging and FST-file plumbing, feeding malformed FST images through real parsers.

#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "dvd/dvd_internal.h"

// ---------------------------------------------------------------------------
// Stubs for platform services.
// ---------------------------------------------------------------------------
static std::vector<u8> gFstImage;

std::vector<u8> dvd_read_fst(const std::string& root) {
    (void)root;
    return gFstImage;
}

extern "C" {
void port_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}

void port_fatal(const char* fmt, ...) {
    (void)fmt;
    abort();
}

void port_host_alloc_scope_enter(void) {}
void port_host_alloc_scope_leave(void) {}
void port_irq_raise(int irq) { (void)irq; }

unsigned char* __PortGXFifoPtr = nullptr;
unsigned char* __PortGXFifoLimit = nullptr;
void __PortGXFifoOverflow(void) {}
}  // extern "C"

// ---------------------------------------------------------------------------
// FST image builder.
// ---------------------------------------------------------------------------
static void put32(std::vector<u8>& v, size_t off, u32 value) {
    v[off + 0] = (u8)(value >> 24);
    v[off + 1] = (u8)(value >> 16);
    v[off + 2] = (u8)(value >> 8);
    v[off + 3] = (u8)value;
}

struct Entry {
    bool dir;
    u32 nameOff;
    u32 parent;
    u32 next;
};

static std::vector<u8> buildFst(u32 count, const std::vector<Entry>& entries, const std::string& names) {
    std::vector<u8> image(count * 12 + names.size(), 0);
    put32(image, 8, count);
    for (size_t i = 0; i < entries.size(); i++) {
        size_t off = i * 12;
        image[off] = entries[i].dir ? 1 : 0;
        put32(image, off, entries[i].nameOff | (entries[i].dir ? 0x01000000u : 0u));
        put32(image, off + 4, entries[i].parent);
        put32(image, off + 8, entries[i].next);
    }
    memcpy(image.data() + count * 12, names.data(), names.size());
    return image;
}

static bool loadExpectingReject(const char* what) {
    dvd_fst_clear();
    bool ok = dvd_load_fst();
    if (ok) {
        printf("[FAIL] %s was accepted\n", what);
        return false;
    }
    if (!sFst.empty()) {
        printf("[FAIL] %s left %zu stale entries behind\n", what, sFst.size());
        return false;
    }
    return true;
}

static void testValidFst() {
    const std::string names = std::string("dir\0file.bin\0", 13);
    std::vector<Entry> e = {
        {true, 0, 0, 3},
        {true, 0, 0, 3},
        {false, 4, 1, 0},
    };
    gFstImage = buildFst(3, e, names);
    dvd_fst_clear();
    assert(dvd_load_fst());
    assert(sFst.size() == 3);
    assert(sFst[2].hostPath == sDataRoot + "/files/dir/file.bin");
    assert(dvd_find_child(0, "dir", 3) == 1);
    assert(dvd_find_child(1, "file.bin", 8) == 2);
    printf("[PASS] Valid FST loads, nests and resolves paths\n");
}

static void testTruncatedAndOversizedTables() {
    gFstImage.assign(7, 0);
    assert(loadExpectingReject("truncated header"));

    // 12-byte header claiming 1e6 entries in 12-byte image
    gFstImage.assign(12, 0);
    put32(gFstImage, 8, 1000000u);
    assert(loadExpectingReject("entry table overrun"));

    // Worst case 32-bit wrap
    gFstImage.assign(12, 0);
    put32(gFstImage, 8, 0xFFFFFFFFu);
    assert(loadExpectingReject("entry count 0xFFFFFFFF"));
    printf("[PASS] Truncated and overflowing FST tables are rejected\n");
}

static void testRootEntryValidation() {
    const std::string names = std::string("dir\0", 4);
    // Root entry marked as a file (isDir == 0)
    {
        std::vector<Entry> e = {{false, 0, 0, 2}, {false, 0, 0, 0}};
        gFstImage = buildFst(2, e, names);
        assert(loadExpectingReject("root is file"));
    }
    // Root entry claiming non-zero parent
    {
        std::vector<Entry> e = {{true, 0, 1, 2}, {false, 0, 0, 0}};
        gFstImage = buildFst(2, e, names);
        assert(loadExpectingReject("root has parent"));
    }
    // Root entry claiming non-zero name offset
    {
        std::vector<Entry> e = {{true, 1, 0, 2}, {false, 0, 0, 0}};
        gFstImage = buildFst(2, e, names);
        assert(loadExpectingReject("root has name offset"));
    }
    printf("[PASS] Malformed root directory entries are rejected\n");
}

static void testBadNames() {
    const std::string names = std::string("ok\0", 3);

    // Name offset past end of string table
    {
        std::vector<Entry> e = {{true, 0, 0, 2}, {false, 0xFFFF, 0, 0}};
        gFstImage = buildFst(2, e, names);
        assert(loadExpectingReject("out-of-range name offset"));
    }
    // Unterminated name
    {
        std::vector<Entry> e = {{true, 0, 0, 2}, {false, 0, 0, 0}};
        gFstImage = buildFst(2, e, "ab");
        assert(loadExpectingReject("unterminated name"));
    }
    // Path separators, colon, and dot traversal
    for (const char* bad : {"a/b", "a\\b", "a:b", ".", ".."}) {
        std::string table(bad);
        table.push_back('\0');
        std::vector<Entry> e = {{true, 0, 0, 2}, {false, 0, 0, 0}};
        gFstImage = buildFst(2, e, table);
        assert(loadExpectingReject(bad));
    }
    printf("[PASS] Unsafe and out-of-range FST names are rejected\n");
}

static void testDirectorySpans() {
    const std::string names = std::string("d\0", 2);

    // next == i
    {
        std::vector<Entry> e = {{true, 0, 0, 2}, {true, 0, 0, 1}};
        gFstImage = buildFst(2, e, names);
        assert(loadExpectingReject("directory next <= own index"));
    }
    // next past table
    {
        std::vector<Entry> e = {{true, 0, 0, 2}, {true, 0, 0, 99}};
        gFstImage = buildFst(2, e, names);
        assert(loadExpectingReject("directory next past table"));
    }
    // Forward parent index
    {
        std::vector<Entry> e = {{true, 0, 0, 3}, {true, 0, 2, 3}, {false, 0, 1, 0}};
        gFstImage = buildFst(3, e, std::string("d\0f\0", 4));
        assert(loadExpectingReject("forward parent index"));
    }
    // Inconsistent parent relationship (parent != parentDir)
    {
        std::vector<Entry> e = {
            {true, 0, 0, 4},
            {true, 0, 0, 4},   // dir 1: parent 0, span 4
            {true, 0, 0, 4},   // dir 2 nested inside dir 1, but claims parent 0 instead of 1!
            {false, 2, 2, 0},
        };
        gFstImage = buildFst(4, e, std::string("d\0e\0f\0", 6));
        assert(loadExpectingReject("inconsistent parent pointer"));
    }
    // Child span escaping parent (next > parent.next)
    {
        std::vector<Entry> e = {
            {true, 0, 0, 5},
            {true, 0, 0, 3},   // dir 1: span [1, 3)
            {true, 0, 1, 4},   // child dir 2 inside dir 1 claims next 4 > 3!
            {false, 2, 1, 0},
            {false, 4, 0, 0},
        };
        gFstImage = buildFst(5, e, std::string("a\0b\0c\0d\0", 8));
        assert(loadExpectingReject("child span escaping parent span"));
    }
    printf("[PASS] Directory parent/next span invariants are enforced\n");
}

static void testFstClearClosesFd() {
    const std::string path = "dvd_test_fd.bin";
    FILE* f = fopen(path.c_str(), "wb");
    assert(f != nullptr);
    fputc('A', f);
    fclose(f);

    dvd_fst_clear();
    sFst.assign(2, FstEntry{});
    sFst[0].isDir = true;
    sFst[1].isDir = false;
    sFst[1].hostPath = path;
    int fd = open(path.c_str(), O_RDONLY);
    assert(fd >= 0);
    sFst[1].fd = fd;

    dvd_fst_clear();
    assert(sFst.empty());
    // Closing fd again must fail with EBADF if dvd_fst_clear closed it
    assert(close(fd) == -1);

    remove(path.c_str());
    printf("[PASS] dvd_fst_clear properly closes cached file descriptors\n");
}

static void testDvdFstHasRootDir() {
    // Valid FST with dir
    const std::string names = std::string("EuEnglish\0Message.arc\0", 22);
    std::vector<Entry> e = {
        {true, 0, 0, 3},
        {true, 0, 0, 3},
        {false, 10, 1, 0},
    };
    std::vector<u8> fst = buildFst(3, e, names);
    assert(dvd_fst_has_root_dir(fst, "EuEnglish"));
    assert(!dvd_fst_has_root_dir(fst, "UsEnglish"));
    assert(!dvd_fst_has_root_dir(fst, nullptr));

    // Overread boundary check: string at very end without padding
    std::vector<u8> truncatedFst = fst;
    // Shrink by 1 byte so null terminator is lost
    truncatedFst.pop_back();
    assert(!dvd_fst_has_root_dir(truncatedFst, "Message.arc"));
    printf("[PASS] dvd_fst_has_root_dir boundary and null checks verified\n");
}

extern void runDvdIoTests();

int main() {
    testValidFst();
    testTruncatedAndOversizedTables();
    testRootEntryValidation();
    testBadNames();
    testDirectorySpans();
    testFstClearClosesFd();
    testDvdFstHasRootDir();
    runDvdIoTests();
    printf("All DVD security and async I/O tests passed.\n");
    return 0;
}
