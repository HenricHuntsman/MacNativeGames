#ifndef GAMEPAD_SDL_H
#define GAMEPAD_SDL_H
#include <stdint.h>

typedef struct LinuxGamepad LinuxGamepad;
typedef struct {
    uint64_t buttons;
    int32_t lx, ly, rx, ry;
    int connected;
} LinuxGamepadState;

/* Starts SDL's game-controller subsystem and opens the first recognized controller. */
LinuxGamepad *gamepad_open(void);
/* Polls hot-plug state and reads the controller; buttons use Switch bit positions 0..15. */
void gamepad_read(LinuxGamepad *gamepad, LinuxGamepadState *out);
const char *gamepad_name(const LinuxGamepad *gamepad);
void gamepad_close(LinuxGamepad *gamepad);
#endif
