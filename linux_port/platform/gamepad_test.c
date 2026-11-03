#define _POSIX_C_SOURCE 200809L
#include "gamepad_sdl.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define EXPECT(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s (SDL: %s)\n", __FILE__, __LINE__, #c, SDL_GetError()); failures++; } } while (0)

int main(void) {
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
    SDL_VirtualJoystickDesc desc = {0};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.nhats = 1;
    desc.name = "Xbox Virtual Test Pad";
    int device = SDL_JoystickAttachVirtualEx(&desc);
    EXPECT(device >= 0);
    SDL_PumpEvents();

    LinuxGamepad *g = gamepad_open();
    EXPECT(g != NULL);
    EXPECT(gamepad_name(g)[0] != '\0');
    LinuxGamepadState state;
    gamepad_read(g, &state);
    EXPECT(state.connected);
    EXPECT(state.buttons == 0);
    EXPECT(state.lx == 0 && state.ly == 0 && state.rx == 0 && state.ry == 0);

    SDL_Joystick *joy = SDL_JoystickOpen(device);
    EXPECT(joy != NULL);
    EXPECT(SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, SDL_PRESSED) == 0);
    EXPECT(SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTX, 32767) == 0);
    SDL_PumpEvents();
    gamepad_read(g, &state);
    EXPECT(state.buttons == (1ull << 1));   /* south button maps to Switch B */
    EXPECT(state.lx > 32000 && state.ly == 0);

    EXPECT(SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, SDL_RELEASED) == 0);
    EXPECT(SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_B, SDL_PRESSED) == 0);
    SDL_PumpEvents();
    gamepad_read(g, &state);
    EXPECT(state.buttons == (1ull << 0));   /* east button maps to Switch A */
    EXPECT(SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_B, SDL_RELEASED) == 0);
    EXPECT(SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_X, SDL_PRESSED) == 0);
    SDL_PumpEvents();
    gamepad_read(g, &state);
    EXPECT(state.buttons == (1ull << 3));   /* west button maps to Switch Y */
    EXPECT(SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_X, SDL_RELEASED) == 0);
    EXPECT(SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_Y, SDL_PRESSED) == 0);
    SDL_PumpEvents();
    gamepad_read(g, &state);
    EXPECT(state.buttons == (1ull << 2));   /* north button maps to Switch X */
    EXPECT(SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_Y, SDL_RELEASED) == 0);
    EXPECT(SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTX, 0) == 0);
    SDL_PumpEvents();
    gamepad_read(g, &state);
    EXPECT(state.buttons == 0 && state.lx == 0);

    gamepad_close(g);

    char path[] = "/tmp/macnativegames-controls-XXXXXX";
    int fd = mkstemp(path);
    EXPECT(fd >= 0);
    const char layout[] = "south = b tap\neast = a toggle\nl2 = zl aim r2\n";
    EXPECT(write(fd, layout, sizeof layout - 1) == (ssize_t)(sizeof layout - 1));
    close(fd);
    EXPECT(setenv("TOTK_CONTROLS", path, 1) == 0);
    g = gamepad_open();
    EXPECT(g != NULL);
    joy = SDL_JoystickOpen(device);
    EXPECT(joy != NULL);

    EXPECT(SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, SDL_PRESSED) == 0);
    SDL_PumpEvents(); gamepad_read(g, &state);
    EXPECT(state.buttons == (1ull << 1));
    EXPECT(SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, SDL_RELEASED) == 0);
    SDL_PumpEvents(); gamepad_read(g, &state);
    EXPECT(state.buttons == (1ull << 1));   /* tap remains active after release */
    SDL_Delay(410); gamepad_read(g, &state);
    EXPECT(state.buttons == 0);             /* tap expires without another input event */

    EXPECT(SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_B, SDL_PRESSED) == 0);
    SDL_PumpEvents(); gamepad_read(g, &state);
    EXPECT(state.buttons == (1ull << 0));
    EXPECT(SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_B, SDL_RELEASED) == 0);
    SDL_PumpEvents(); gamepad_read(g, &state);
    EXPECT(state.buttons == (1ull << 0));   /* toggle remains active */
    EXPECT(SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTX, 32767) == 0);
    SDL_PumpEvents(); gamepad_read(g, &state);
    EXPECT(state.buttons == (1ull << 0));
    EXPECT(SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTX, 0) == 0);
    SDL_PumpEvents(); gamepad_read(g, &state);
    EXPECT(state.buttons == 0);             /* toggle ends when movement returns to center */

    EXPECT(SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767) == 0);
    SDL_PumpEvents(); gamepad_read(g, &state);
    EXPECT(state.buttons == (1ull << 8));
    EXPECT(SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767) == 0);
    SDL_PumpEvents(); gamepad_read(g, &state);
    EXPECT(state.buttons == 0);             /* aim is released and fire is suppressed during the shot */

    gamepad_close(g);
    SDL_JoystickClose(joy);
    EXPECT(SDL_JoystickDetachVirtual(device) == 0);
    unsetenv("TOTK_CONTROLS");
    unlink(path);
    SDL_Quit();
    printf(failures ? "gamepad_test: %d failure(s)\n" : "gamepad_test: all passed\n", failures);
    return failures != 0;
}
