// Host test for the DVD layer's untrusted-input handling.
//
// Builds the production dvd_fst.cpp / dvd_read.cpp against stubs for the host
// allocation, logging and FST-file plumbing, then feeds malformed FST images
// and invalid read requests through the real parsers.
//
//   ninja -C build-host dvd_fst_test && ./build-host/dvd_fst_test

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "dvd/dvd_internal.h"

// ---------------------------------------------------------------------------
// Stubs for the platform services the DVD layer calls.
// ---------------------------------------------------------------------------
static std::vector<u8> gFstImage;

std::vector<u8> dvd_read_fst(const std::string& root) {
    (void)root;
    return gFstImage;
}

u32 dvd_be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

// These are declared with C linkage by port/port.h and port/heap_routing.h.
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

// port/port.h pulls in the GX FIFO helpers; nothing here writes to the FIFO.
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
    sFst.clear();
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
    sFst.clear();
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

    // 12-byte header claiming 1e6 entries in a 12-byte image: the entry table
    // must be proven to fit before any pointer arithmetic.
    gFstImage.assign(12, 0);
    put32(gFstImage, 8, 1000000u);
    assert(loadExpectingReject("entry table overrun"));

    // Worst case for 32-bit overflow: count * 12 would wrap.
    gFstImage.assign(12, 0);
    put32(gFstImage, 8, 0xFFFFFFFFu);
    assert(loadExpectingReject("entry count 0xFFFFFFFF"));
    printf("[PASS] Truncated and overflowing FST tables are rejected\n");
}

static void testBadNames() {
    const std::string names = std::string("ok\0", 3);

    // Name offset past the end of the string table.
    {
        std::vector<Entry> e = {{true, 0, 0, 2}, {false, 0xFFFF, 0, 0}};
        gFstImage = buildFst(2, e, names);
        assert(loadExpectingReject("out-of-range name offset"));
    }
    // Name without a terminator inside the buffer: the image ends right after
    // the two characters, so no NUL exists in the string table.
    {
        std::vector<Entry> e = {{true, 0, 0, 2}, {false, 0, 0, 0}};
        gFstImage = buildFst(2, e, "ab");
        assert(loadExpectingReject("unterminated name"));
    }
    // Path separators and parent traversal in a directory entry name.
    for (const char* bad : {"a/b", "a\\b", ".."}) {
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

    // next == i would make DVDReadDir/dvd_find_child spin on the same entry.
    {
        std::vector<Entry> e = {{true, 0, 0, 2}, {true, 0, 0, 1}};
        gFstImage = buildFst(2, e, names);
        assert(loadExpectingReject("directory next <= own index"));
    }
    // next past the table would walk out of bounds.
    {
        std::vector<Entry> e = {{true, 0, 0, 2}, {true, 0, 0, 99}};
        gFstImage = buildFst(2, e, names);
        assert(loadExpectingReject("directory next past the table"));
    }
    // A parent that does not precede the child breaks the preorder invariant.
    {
        std::vector<Entry> e = {{true, 0, 0, 3}, {true, 0, 2, 3}, {false, 2, 1, 0}};
        gFstImage = buildFst(3, e, std::string("d\0f\0", 4));
        assert(loadExpectingReject("forward parent index"));
    }
    printf("[PASS] Directory parent/next span invariants are enforced\n");
}

static void testReadValidation() {
    const std::string path = "dvd_fst_test_payload.bin";
    FILE* f = fopen(path.c_str(), "wb");
    assert(f != nullptr);
    std::vector<u8> payload(100, 0x5A);
    assert(fwrite(payload.data(), 1, payload.size(), f) == payload.size());
    fclose(f);

    sFst.assign(2, FstEntry{});
    sFst[0].isDir = true;
    sFst[1].hostPath = path;

    DVDFileInfo fi;
    memset(&fi, 0, sizeof(fi));
    fi.startAddr = 1;
    std::vector<u8> buffer(128, 0);

    assert(dvd_io_validate(&fi, buffer.data(), 100, 0, "test"));
    assert(dvd_io_validate(&fi, buffer.data(), 0, 100, "test"));
    assert(!dvd_io_validate(&fi, buffer.data(), -1, 0, "test"));
    assert(!dvd_io_validate(&fi, buffer.data(), 4, -8, "test"));
    assert(!dvd_io_validate(&fi, buffer.data(), 101, 0, "test"));
    assert(!dvd_io_validate(&fi, buffer.data(), 8, 96, "test"));
    assert(!dvd_io_validate(&fi, nullptr, 4, 0, "test"));

    // A directory index is refused outright: it has no readable payload.
    fi.startAddr = 0;  // directory
    assert(!dvd_io_validate(&fi, buffer.data(), 1, 0, "test"));

    remove(path.c_str());
    printf("[PASS] Negative, null and out-of-range reads are rejected\n");
}

int main() {
    testValidFst();
    testTruncatedAndOversizedTables();
    testBadNames();
    testDirectorySpans();
    testReadValidation();
    printf("All DVD input-validation tests passed.\n");
    return 0;
}
