CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDLIBS = $(shell pkg-config --libs sdl2 vulkan)
CFLAGS += $(shell pkg-config --cflags sdl2 vulkan)

vk_clear: vk_clear.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f vk_clear platform/vm_test platform/audio_test platform/gamepad_test vulkan/clear_probe

.PHONY: clean test test-audio test-gamepad test-vulkan

platform/vm_test: platform/vm_test.c platform/vm_linux.c platform/vm_linux.h
	$(CC) $(CFLAGS) -o $@ platform/vm_test.c platform/vm_linux.c

test: platform/vm_test platform/gamepad_test platform/audio_test vulkan/clear_probe
	./platform/vm_test
	./platform/gamepad_test
	SDL_AUDIODRIVER=dummy ./platform/audio_test --silent
	./vulkan/clear_probe

platform/audio_test: platform/audio_test.c platform/audio_sdl.c platform/audio_sdl.h
	$(CC) $(CFLAGS) -o $@ platform/audio_test.c platform/audio_sdl.c $(LDLIBS) -lm

test-audio: platform/audio_test
	SDL_AUDIODRIVER=$${SDL_AUDIODRIVER:-dummy} ./platform/audio_test --silent

platform/gamepad_test: platform/gamepad_test.c platform/gamepad_sdl.c platform/gamepad_sdl.h ../games/totk/runtime/controller_map.h ../games/totk/runtime/gamepad_layout.h
	$(CC) $(CFLAGS) -o $@ platform/gamepad_test.c platform/gamepad_sdl.c $(LDLIBS)

test-gamepad: platform/gamepad_test
	SDL_VIDEODRIVER=$${SDL_VIDEODRIVER:-dummy} ./platform/gamepad_test

vulkan/clear_probe: vulkan/clear_probe.c
	$(CC) $(CFLAGS) -o $@ $< $(shell pkg-config --libs vulkan)

test-vulkan: vulkan/clear_probe
	./vulkan/clear_probe
