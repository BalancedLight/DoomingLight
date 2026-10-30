#ifndef DOOM_MODE_SYNTH_H
#define DOOM_MODE_SYNTH_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Offline MUS rendering. The returned stereo PCM is malloc-owned. */
int doom_synth_render_mus(const uint8_t *mus, size_t mus_size,
                          const uint8_t *genmidi, size_t genmidi_size,
                          int16_t **pcm, size_t *frames, unsigned rate);
int doom_synth_render(const uint8_t *music, size_t music_size,
                      const uint8_t *genmidi, size_t genmidi_size,
                      int16_t **pcm, size_t *frames, unsigned rate);
typedef int (*DoomSynthCancelFn)(void *context);
int doom_synth_render_cancelable(const uint8_t *music, size_t music_size,
                                 const uint8_t *genmidi, size_t genmidi_size,
                                 int16_t **pcm, size_t *frames, unsigned rate,
                                 DoomSynthCancelFn canceled, void *context);
#ifdef __cplusplus
}
#endif
#endif
