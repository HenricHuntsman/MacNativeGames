#include "gamepad_sdl.h"
#include "../../games/totk/runtime/controller_map.h"
#include "../../games/totk/runtime/gamepad_layout.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct LinuxGamepad {
    SDL_GameController *controller;
    int owns_subsystem;
    uint16_t map[CONTROLLER_BUTTONS];
    uint8_t modes[CONTROLLER_BUTTONS];
    char name[128];
};

static int32_t axis_value(Sint16 value) {
    const float deadzone = 0.12f;
    float normalized = value < 0 ? (float)value / 32768.0f : (float)value / 32767.0f;
    float magnitude = fabsf(normalized);
    if (magnitude <= deadzone) return 0;
    if (magnitude > 1.0f) magnitude = 1.0f;
    int32_t result = (int32_t)((magnitude - deadzone) / (1.0f - deadzone) * 32767.0f);
    return normalized < 0 ? -result : result;
}

static int face_by_position(SDL_GameController *controller) {
    const char *name = SDL_GameControllerName(controller);
    const char *setting = getenv("TOTK_FACE_BUTTONS");
    if (setting && !strcmp(setting, "label")) return 0;
    if (setting && !strcmp(setting, "position")) return 1;
    SDL_GameControllerType type = SDL_GameControllerGetType(controller);
    if (type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO || type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
        type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT || type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR)
        return 0;
    return gamepad_face_by_position(name, name);
}

static int open_first(LinuxGamepad *g) {
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i)) continue;
        g->controller = SDL_GameControllerOpen(i);
        if (!g->controller) continue;
        const char *name = SDL_GameControllerName(g->controller);
        snprintf(g->name, sizeof g->name, "%s", name ? name : "Controller");
        fprintf(stderr, "[input] SDL controller: %s\n", g->name);
        return 1;
    }
    g->name[0] = 0;
    return 0;
}

static void load_map(LinuxGamepad *g) {
    controller_map_default(g->map, g->modes);
    const char *path = getenv("TOTK_CONTROLS");
    if (!path || !*path) return;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char text[16384]; size_t n = fread(text, 1, sizeof text - 1, f); int complete = feof(f); fclose(f);
    if (!complete) { fprintf(stderr, "[input] controller layout too large: %s\n", path); return; }
    text[n] = 0;
    const char *error = NULL;
    int line = controller_map_parse(text, g->map, g->modes, &error);
    if (line) { fprintf(stderr, "[input] %s line %d: %s\n", path, line, error); controller_map_default(g->map, g->modes); }
}

LinuxGamepad *gamepad_open(void) {
    LinuxGamepad *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    if (!(SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER)) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            fprintf(stderr, "[input] SDL game-controller init: %s\n", SDL_GetError());
            free(g); return NULL;
        }
        g->owns_subsystem = 1;
    }
    load_map(g);
    open_first(g);
    return g;
}

void gamepad_read(LinuxGamepad *g, LinuxGamepadState *out) {
    if (!g || !out) return;
    SDL_GameControllerUpdate();
    if (!g->controller || !SDL_GameControllerGetAttached(g->controller)) {
        if (g->controller) SDL_GameControllerClose(g->controller);
        g->controller = NULL;
        open_first(g);
    }
    *out = (LinuxGamepadState){0};
    if (!g->controller) return;

    SDL_GameController *c = g->controller;
    SDL_GameControllerButton a = SDL_CONTROLLER_BUTTON_A, b = SDL_CONTROLLER_BUTTON_B;
    SDL_GameControllerButton x = SDL_CONTROLLER_BUTTON_X, y = SDL_CONTROLLER_BUTTON_Y;
    uint64_t physical = gamepad_face_positions(SDL_GameControllerGetButton(c, a), SDL_GameControllerGetButton(c, b),
                                                SDL_GameControllerGetButton(c, x), SDL_GameControllerGetButton(c, y),
                                                face_by_position(c));
#define PAD_BUTTON(sdl, bit) do { if (SDL_GameControllerGetButton(c, (sdl))) physical |= 1ull << (bit); } while (0)
    PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSTICK, 4);
    PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSTICK, 5);
    PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 6);
    PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 7);
    PAD_BUTTON(SDL_CONTROLLER_BUTTON_BACK, 10);
    PAD_BUTTON(SDL_CONTROLLER_BUTTON_START, 11);
    PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 12);
    PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_UP, 13);
    PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 14);
    PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_DOWN, 15);
#undef PAD_BUTTON
    if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0) physical |= 1ull << 8;
    if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0) physical |= 1ull << 9;
    out->buttons = controller_map_apply(g->map, physical);
    out->lx = axis_value(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX));
    out->ly = -axis_value(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
    out->rx = axis_value(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX));
    out->ry = -axis_value(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY));
    out->connected = 1;
}

const char *gamepad_name(const LinuxGamepad *g) { return g && g->controller ? g->name : ""; }

void gamepad_close(LinuxGamepad *g) {
    if (!g) return;
    if (g->controller) SDL_GameControllerClose(g->controller);
    if (g->owns_subsystem) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    free(g);
}
