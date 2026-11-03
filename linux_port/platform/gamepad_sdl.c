#include "gamepad_sdl.h"
#include "../../games/totk/runtime/controller_map.h"
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
    uint64_t physical_prev, tap_until[CONTROLLER_BUTTONS];
    uint8_t toggle_on[CONTROLLER_BUTTONS], toggle_moved[CONTROLLER_BUTTONS];
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

static uint64_t face_button_bits(int a, int b, int x, int y) {
    return (a ? 1ull << 0 : 0) | (b ? 1ull << 1 : 0) | (x ? 1ull << 2 : 0) | (y ? 1ull << 3 : 0);
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

static uint64_t mapped_buttons(LinuxGamepad *g, uint64_t physical, int moving, int connected) {
    uint64_t now = SDL_GetTicks64(), edges = physical & ~g->physical_prev, synthetic = 0, suppressed = 0;
    g->physical_prev = physical;
    for (int i = 0; i < CONTROLLER_BUTTONS; i++) {
        uint64_t bit = 1ull << i;
        int kind = controller_mode_kind(g->modes[i]);
        if (kind == CONTROLLER_TAP && (edges & bit)) g->tap_until[i] = now + 400;
        if (kind == CONTROLLER_TOGGLE) {
            if (edges & bit) { g->toggle_on[i] = !g->toggle_on[i]; g->toggle_moved[i] = moving; }
            if (g->toggle_on[i] && moving) g->toggle_moved[i] = 1;
            if (g->toggle_on[i] && ((g->toggle_moved[i] && !moving) || !connected)) g->toggle_on[i] = 0;
        }
        if (kind == CONTROLLER_AIM && (physical & bit)) {
            uint64_t fire = 1ull << controller_mode_fire(g->modes[i]);
            suppressed |= fire;
            if (!(physical & fire)) synthetic |= g->map[i];
        }
        int active = kind == CONTROLLER_TAP ? now < g->tap_until[i] : kind == CONTROLLER_TOGGLE ? g->toggle_on[i] : 0;
        if (active) synthetic |= g->map[i];
    }
    uint64_t held = physical & ~suppressed;
    for (int i = 0; i < CONTROLLER_BUTTONS; i++)
        if (controller_mode_kind(g->modes[i]) != CONTROLLER_HOLD) held &= ~(1ull << i);
    return controller_map_apply(g->map, held) | synthetic;
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
        g->physical_prev = 0;
        memset(g->tap_until, 0, sizeof g->tap_until);
        memset(g->toggle_on, 0, sizeof g->toggle_on);
        memset(g->toggle_moved, 0, sizeof g->toggle_moved);
        open_first(g);
    }
    *out = (LinuxGamepadState){0};
    if (!g->controller) return;

    SDL_GameController *c = g->controller;
    SDL_GameControllerButton a = SDL_CONTROLLER_BUTTON_A, b = SDL_CONTROLLER_BUTTON_B;
    SDL_GameControllerButton x = SDL_CONTROLLER_BUTTON_X, y = SDL_CONTROLLER_BUTTON_Y;
    uint64_t physical = face_button_bits(SDL_GameControllerGetButton(c, a), SDL_GameControllerGetButton(c, b),
                                         SDL_GameControllerGetButton(c, x), SDL_GameControllerGetButton(c, y));
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
    out->lx = axis_value(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX));
    out->ly = -axis_value(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
    out->rx = axis_value(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX));
    out->ry = -axis_value(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY));
    out->connected = 1;
    out->buttons = mapped_buttons(g, physical, out->lx || out->ly, 1);
}

const char *gamepad_name(const LinuxGamepad *g) { return g && g->controller ? g->name : ""; }

void gamepad_close(LinuxGamepad *g) {
    if (!g) return;
    if (g->controller) SDL_GameControllerClose(g->controller);
    if (g->owns_subsystem) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    free(g);
}
