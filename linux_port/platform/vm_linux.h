/* Linux replacements for the Mach VM calls the runtime uses (loader.c, gfx_bridge.c, game_patches*.c):
   mach_vm_allocate -> vm_reserve / vm_alloc, mach_vm_remap -> vm_alias,
   mach_vm_region -> vm_region, mach_vm_read_overwrite -> vm_read_safe. */
#ifndef VM_LINUX_H
#define VM_LINUX_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct { uint64_t start, size; int prot; } vm_region_t;   /* prot: PROT_READ | PROT_WRITE | PROT_EXEC */

/* Reserve [addr, addr+size) as PROT_NONE without replacing anything. NULL when the range is taken. */
void *vm_reserve(uint64_t addr, size_t size);
/* Zeroed read/write memory at any address. It is shared (memfd), so vm_alias can map it a second time. */
void *vm_alloc(size_t size);
/* Map the pages at `from` (from vm_alloc) a second time at `to`, replacing what is there. 0 on success. */
int vm_alias(void *from, uint64_t to, size_t size);
int vm_protect(uint64_t addr, size_t size, int prot);
/* The first mapped region ending above `addr` (start is raised to addr when addr lies inside it). 0 on success, -1 at the end. */
int vm_region(uint64_t addr, vm_region_t *out);
/* Copy n bytes from any address of this process, a bad pointer gives a short count instead of a fault. */
ssize_t vm_read_safe(uint64_t addr, void *dst, size_t n);
#endif
