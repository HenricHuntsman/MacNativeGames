CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDLIBS = $(shell pkg-config --libs sdl2 vulkan)
CFLAGS += $(shell pkg-config --cflags sdl2 vulkan)

vk_clear: vk_clear.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f vk_clear

.PHONY: clean
