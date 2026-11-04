/* Plays a 440 Hz tone through audio_sdl for 2 seconds. `audio_test --silent` checks the pipeline without needing a sound card
   (use SDL_AUDIODRIVER=dummy). Pass/fail comes from the callback's frame count, not from listening. */
#include "audio_sdl.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

typedef struct { _Atomic uint64_t frames; } CallbackState;

static void silent_render(void *ctx, float *out, unsigned frames) {
    CallbackState *state = ctx;
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    atomic_fetch_add(&state->frames, frames);
}

int main(int argc, char **argv) {
    const int rate = 48000, seconds = 2, block = rate * 5 / 1000;
    int silent = argc > 1 && !strcmp(argv[1], "--silent");
    LinuxAudio *a = audio_open(rate, 2);
    if (!a) { fprintf(stderr, "audio_test: no audio device\n"); return 1; }
    audio_set_volume(a, silent ? 0.0f : 0.2f);

    float buf[480 * 2]; double phase = 0; uint64_t sent = 0;
    Uint32 start = SDL_GetTicks();
    while (sent < (uint64_t)rate * seconds) {
        for (int i = 0; i < block; i++) { float s = (float)sin(phase); phase += 2 * 3.14159265358979323846 * 440.0 / rate; buf[2 * i] = buf[2 * i + 1] = s; }
        size_t took = audio_write(a, buf, (size_t)block);
        sent += took;
        SDL_Delay(took ? 4 : 2);   /* a little faster than real time so the ring stays filled */
    }
    SDL_Delay(100);
    LinuxAudioStats st; audio_stats(a, &st);
    printf("audio_test: wrote %llu frames, played %llu, underruns %llu, overruns %llu, trims %llu, %u ms\n",
           (unsigned long long)sent, (unsigned long long)st.played, (unsigned long long)st.underruns,
           (unsigned long long)st.overruns, (unsigned long long)st.trims, SDL_GetTicks() - start);
    audio_close(a);
    int ok = st.played > (uint64_t)rate * seconds * 8 / 10;
    printf(ok ? "audio_test: passed\n" : "audio_test: FAILED (the device consumed too little)\n");
    if (!ok) return 1;

    CallbackState callback_state = {0};
    a = audio_open_callback(rate, 2, silent_render, &callback_state);
    if (!a) { fprintf(stderr, "audio_test: callback device open failed\n"); return 1; }
    SDL_Delay(120);
    audio_stats(a, &st);
    uint64_t rendered = atomic_load(&callback_state.frames);
    audio_close(a);
    printf("audio_test: direct callback rendered %llu frames\n", (unsigned long long)rendered);
    if (rendered < (uint64_t)rate / 20 || st.played < rendered) {
        fprintf(stderr, "audio_test: FAILED (direct callback did not run)\n");
        return 1;
    }
    printf("audio_test: callback mode passed\n");
    return 0;
}
