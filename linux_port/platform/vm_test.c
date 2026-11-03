/* Game-data-free check of vm_linux.c, using the guest address layout of the runtime (alias region at 0x70_0000_0000). */
#include "vm_linux.h"
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static int failures;
#define EXPECT(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void) {
    const uint64_t alias_base = 0x7000000000ull;
    const size_t size = 64u << 20;

    EXPECT(vm_reserve(alias_base, size) != NULL);
    EXPECT(vm_reserve(alias_base, size) == NULL);          /* taken ranges are refused */

    uint8_t *a = vm_alloc(size);
    EXPECT(a != NULL);
    EXPECT(vm_alias(a, alias_base, size) == 0);
    uint8_t *b = (uint8_t *)(uintptr_t)alias_base;
    a[4096] = 0x5a; EXPECT(b[4096] == 0x5a);               /* both views share the pages */
    b[8192] = 0xa5; EXPECT(a[8192] == 0xa5);

    vm_region_t r;
    EXPECT(vm_region(alias_base, &r) == 0 && r.start == alias_base && r.size >= size && (r.prot & PROT_WRITE));
    EXPECT(vm_region(alias_base + 4096, &r) == 0 && r.start == alias_base + 4096);

    uint8_t buf[16];
    EXPECT(vm_read_safe(alias_base + 4096, buf, 1) == 1 && buf[0] == 0x5a);
    EXPECT(vm_read_safe(0x10, buf, sizeof buf) == 0);      /* unmapped: no fault */

    EXPECT(vm_protect(alias_base, 4096, PROT_READ) == 0);
    EXPECT(vm_region(alias_base, &r) == 0 && r.size == 4096 && r.prot == PROT_READ);

    printf(failures ? "vm_test: %d failure(s)\n" : "vm_test: all passed\n", failures);
    return failures != 0;
}
