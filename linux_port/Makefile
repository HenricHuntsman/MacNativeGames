CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDLIBS = $(shell pkg-config --libs sdl2 vulkan)
CFLAGS += $(shell pkg-config --cflags sdl2 vulkan)

vk_clear: vk_clear.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f vk_clear platform/vm_test

.PHONY: clean

platform/vm_test: platform/vm_test.c platform/vm_linux.c platform/vm_linux.h
	$(CC) $(CFLAGS) -o $@ platform/vm_test.c platform/vm_linux.c

test: platform/vm_test
	./platform/vm_test
