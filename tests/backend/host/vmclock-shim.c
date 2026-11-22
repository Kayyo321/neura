// Test-only VMClock shim. Other descriptor I/O passes through. Fail if a
// process opens the cached device twice or reads it at a nonzero offset.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int fake = -1;
static pid_t owner;
static int opens;

static int selected(const char* mode) {
    const char* configured = getenv("VMCLOCK_MODE");
    return configured != NULL && strcmp(configured, mode) == 0;
}

int open(const char* path, int flags, ...) {
    int (*real_open)(const char*, int, ...) = dlsym(RTLD_NEXT, "open");
    mode_t mode = 0;
    if ((flags & O_CREAT) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    if (strcmp(path, "/dev/vmclock0") != 0) return real_open(path, flags, mode);
    if (owner != getpid()) {
        owner = getpid();
        opens = 0;
    }
    if (++opens != 1) {
        fprintf(stderr, "VMClock opened repeatedly\n");
        _Exit(81);
    }
    if (selected("denied")) {
        errno = EACCES;
        return -1;
    }
    fake = real_open("/dev/zero", flags, mode);
    return fake;
}

ssize_t pread(int descriptor, void* data, size_t count, off_t offset) {
    ssize_t (*real_pread)(int, void*, size_t, off_t) = dlsym(RTLD_NEXT, "pread");
    if (descriptor != fake) return real_pread(descriptor, data, count, offset);
    if (offset != 0 || count < 112) _Exit(82);
    unsigned char bytes[112] = {0};
    bytes[0] = 'V';
    bytes[1] = 'C';
    bytes[2] = 'L';
    bytes[3] = 'K';
    bytes[4] = 112;
    bytes[8] = 1;
    bytes[25] = 1; // VM_GEN_COUNTER_PRESENT at bit 8 of flags.
    bytes[104] = 77;
    memcpy(data, bytes, sizeof bytes);
    return sizeof bytes;
}
