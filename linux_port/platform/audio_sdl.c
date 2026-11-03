#include "audio_sdl.h"
#include <SDL2/SDL.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RING_FRAMES 16384   /* about 340 ms at 48 kHz; a power of two so the index wraps with a mask */
#define BLOCK_MS 5

struct LinuxAudio {
    SDL_AudioDeviceID dev;
    int rate, channels;
    float *ring;
    _Atomic uint64_t wr, rd, played, underruns, overruns, trims;
    _Atomic int hold, prefill_ms;
    _Atomic float volume;
    int ready;   /* callback thread only */
};

static void callback(void *ctx, Uint8 *stream, int len) {
    LinuxAudio *a = ctx;
    float *out = (float *)stream;
    const int ch = a->channels;
    const uint64_t frames = (uint64_t)len / (sizeof(float) * (size_t)ch);
    uint64_t rd = atomic_load(&a->rd), wr = atomic_load_explicit(&a->wr, memory_order_acquire), have = wr - rd;

    if (atomic_load(&a->hold)) { memset(out, 0, (size_t)len); a->ready = 0; atomic_store(&a->rd, wr); return; }

    uint64_t block = (uint64_t)a->rate * BLOCK_MS / 1000;
    uint64_t prefill = (uint64_t)atomic_load(&a->prefill_ms) * (uint64_t)a->rate / 1000;
    if (prefill < frames) prefill = frames;
    if (prefill > RING_FRAMES) prefill = RING_FRAMES;
    if (!a->ready) {
        if (have < prefill) { memset(out, 0, (size_t)len); return; }
        a->ready = 1;
    }
    if (have > prefill + block * 6) {   /* backlog after a stall: keep the prefill, drop the rest */
        atomic_fetch_add(&a->trims, 1);
        rd = wr - prefill; have = prefill;
    }
    uint64_t n = frames < have ? frames : have;
    float vol = atomic_load(&a->volume);
    for (uint64_t i = 0; i < n; i++) {
        const float *src = &a->ring[(size_t)((rd + i) & (RING_FRAMES - 1)) * (size_t)ch];
        for (int c = 0; c < ch; c++) out[i * (uint64_t)ch + (uint64_t)c] = src[c] * vol;
    }
    if (n < frames) {
        memset(out + n * (uint64_t)ch, 0, (size_t)((frames - n) * (uint64_t)ch * sizeof(float)));
        atomic_fetch_add(&a->underruns, 1);
        a->ready = 0;
    }
    atomic_fetch_add(&a->played, n);
    atomic_store_explicit(&a->rd, rd + n, memory_order_release);
}

LinuxAudio *audio_open(int rate, int channels) {
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { fprintf(stderr, "[audio] SDL: %s\n", SDL_GetError()); return NULL; }
    if (channels != 2 && channels != 6) {
        SDL_AudioSpec dev_spec;
        channels = (SDL_GetAudioDeviceSpec(0, 0, &dev_spec) == 0 && dev_spec.channels >= 6) ? 6 : 2;
    }
    LinuxAudio *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->ring = calloc((size_t)RING_FRAMES * 6, sizeof(float));
    if (!a->ring) { free(a); return NULL; }
    a->rate = rate; a->channels = channels;
    atomic_store(&a->prefill_ms, 20); atomic_store(&a->volume, 1.0f);

    SDL_AudioSpec want = { .freq = rate, .format = AUDIO_F32SYS, .channels = (Uint8)channels, .samples = 512, .callback = callback, .userdata = a }, got;
    a->dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);   /* no changes allowed: SDL converts to the device */
    if (!a->dev) { fprintf(stderr, "[audio] SDL_OpenAudioDevice: %s\n", SDL_GetError()); free(a->ring); free(a); return NULL; }
    fprintf(stderr, "[audio] %s output, %d Hz, %d channels\n", SDL_GetCurrentAudioDriver(), rate, channels);
    SDL_PauseAudioDevice(a->dev, 0);
    return a;
}

int audio_channels(const LinuxAudio *a) { return a->channels; }

size_t audio_write(LinuxAudio *a, const float *frames, size_t n) {
    if (atomic_load(&a->hold)) return n;   /* dropped, as the CoreAudio stage does while held */
    uint64_t wr = atomic_load(&a->wr), rd = atomic_load_explicit(&a->rd, memory_order_acquire);
    uint64_t room = RING_FRAMES - (wr - rd);
    size_t take = n < room ? n : (size_t)room;
    if (take < n) atomic_fetch_add(&a->overruns, 1);
    const size_t ch = (size_t)a->channels;
    for (size_t i = 0; i < take; i++) memcpy(&a->ring[(size_t)((wr + i) & (RING_FRAMES - 1)) * ch], &frames[i * ch], ch * sizeof(float));
    atomic_store_explicit(&a->wr, wr + take, memory_order_release);
    return take;
}

void audio_hold(LinuxAudio *a, int on) { atomic_store(&a->hold, on != 0); }
void audio_set_volume(LinuxAudio *a, float v) { atomic_store(&a->volume, v); }
void audio_set_prefill_ms(LinuxAudio *a, int ms) { atomic_store(&a->prefill_ms, ms < 0 ? 0 : ms > 500 ? 500 : ms); }

void audio_stats(const LinuxAudio *a, LinuxAudioStats *out) {
    out->played = atomic_load(&a->played); out->underruns = atomic_load(&a->underruns);
    out->overruns = atomic_load(&a->overruns); out->trims = atomic_load(&a->trims);
}

void audio_close(LinuxAudio *a) {
    if (!a) return;
    SDL_CloseAudioDevice(a->dev);
    free(a->ring); free(a);
}
