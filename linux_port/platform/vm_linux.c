#define _GNU_SOURCE
#include "vm_linux.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

void *vm_reserve(uint64_t addr, size_t size) {
    void *p = mmap((void *)(uintptr_t)addr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) return NULL;
    if ((uint64_t)(uintptr_t)p != addr) { munmap(p, size); return NULL; }   /* kernels before 4.17 treat the address as a hint */
    return p;
}

void *vm_alloc(size_t size) {
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

int vm_alias(void *from, uint64_t to, size_t size) {
    /* old_size 0 duplicates a shared mapping instead of moving it */
    void *p = mremap(from, 0, size, MREMAP_MAYMOVE | MREMAP_FIXED, (void *)(uintptr_t)to);
    return p == MAP_FAILED || (uint64_t)(uintptr_t)p != to ? -1 : 0;
}

int vm_protect(uint64_t addr, size_t size, int prot) { return mprotect((void *)(uintptr_t)addr, size, prot); }

int vm_region(uint64_t addr, vm_region_t *out) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return -1;
    char line[512]; int found = -1;
    while (fgets(line, sizeof line, f)) {
        unsigned long long lo, hi; char perms[5];
        if (sscanf(line, "%llx-%llx %4s", &lo, &hi, perms) != 3 || hi <= addr) continue;
        out->start = lo < addr ? addr : lo;
        out->size = hi - out->start;
        out->prot = (perms[0] == 'r' ? PROT_READ : 0) | (perms[1] == 'w' ? PROT_WRITE : 0) | (perms[2] == 'x' ? PROT_EXEC : 0);
        found = 0; break;   /* /proc/self/maps is sorted by address */
    }
    fclose(f);
    return found;
}

ssize_t vm_read_safe(uint64_t addr, void *dst, size_t n) {
    struct iovec local = { dst, n }, remote = { (void *)(uintptr_t)addr, n };
    ssize_t got = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
    return got < 0 ? 0 : got;
}
