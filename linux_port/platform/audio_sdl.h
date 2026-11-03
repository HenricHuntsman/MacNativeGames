/* SDL2 audio output for the Linux port, the counterpart of the CoreAudio stage in each game's runtime/audio_mixer.c.
   The mixer renders 5 ms blocks and calls audio_write(); the device callback drains a lock-free ring buffer,
   waits for a prefill before it starts and after an underrun, and drops a backlog left by a device stall. */
#ifndef AUDIO_SDL_H
#define AUDIO_SDL_H
#include <stddef.h>
#include <stdint.h>

typedef struct LinuxAudio LinuxAudio;
typedef struct { uint64_t played, underruns, overruns, trims; } LinuxAudioStats;
typedef void (*LinuxAudioRender)(void *ctx, float *out, unsigned frames);

/* channels: 2, 6, or 0 to use 6 only when the output device has at least 6. NULL when no device can be opened. */
LinuxAudio *audio_open(int rate, int channels);
LinuxAudio *audio_open_callback(int rate, int channels, LinuxAudioRender render, void *ctx);
int audio_detect_channels(void);
int audio_channels(const LinuxAudio *a);
/* Interleaved float frames. Returns how many fit; the rest are dropped by the caller or retried. */
size_t audio_write(LinuxAudio *a, const float *frames, size_t n);
/* While on, the device plays silence and writes are discarded (start-up, shader warm-up). */
void audio_hold(LinuxAudio *a, int on);
void audio_set_volume(LinuxAudio *a, float v);
/* Milliseconds of audio queued before playback starts; default 20. */
void audio_set_prefill_ms(LinuxAudio *a, int ms);
void audio_stats(const LinuxAudio *a, LinuxAudioStats *out);
void audio_close(LinuxAudio *a);
#endif
