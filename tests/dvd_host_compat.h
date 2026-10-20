// Test-only shims so the production DVD sources can be compiled and exercised
// on a Windows host.  The Android build compiles them against bionic, which
// provides these directly; nothing here is part of the shipped binary.
#pragma once

#ifdef _WIN32

#include <io.h>
#include <stdio.h>
#include <sys/types.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

static inline ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
    if (_lseeki64(fd, (long long)offset, SEEK_SET) < 0) {
        return -1;
    }
    return (ssize_t)_read(fd, buf, (unsigned)count);
}

#endif
