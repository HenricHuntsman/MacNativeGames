#include "gamepad_sdl.h"
#include <SDL2/SDL.h>
#include <stdio.h>

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
    EXPECT(SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTX, 0) == 0);
    SDL_PumpEvents();
    gamepad_read(g, &state);
    EXPECT(state.buttons == 0 && state.lx == 0);

    gamepad_close(g);
    SDL_JoystickClose(joy);
    EXPECT(SDL_JoystickDetachVirtual(device) == 0);
    SDL_Quit();
    printf(failures ? "gamepad_test: %d failure(s)\n" : "gamepad_test: all passed\n", failures);
    return failures != 0;
}
