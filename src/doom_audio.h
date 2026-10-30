#ifndef DOOM_MODE_AUDIO_H
#define DOOM_MODE_AUDIO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomAudio DoomAudio;
DoomAudio *doom_audio_create(void);
void doom_audio_destroy(DoomAudio *audio);
/* Copies mono PCM before returning. Limit each sound to 60 seconds. */
int doom_audio_play_pcm(DoomAudio *audio, const int16_t *samples,
                        size_t count, unsigned sample_rate, float volume);
/* One looping weapon sound at a time. Repeating a clip ID only changes volume. */
int doom_audio_weapon_loop_pcm(DoomAudio *audio, unsigned clip_id,
                               const int16_t *samples, size_t count,
                               unsigned sample_rate, float volume);
int doom_audio_weapon_once_pcm(DoomAudio *audio, unsigned clip_id,
                               const int16_t *samples, size_t count,
                               unsigned sample_rate, float volume);
void doom_audio_weapon_stop(DoomAudio *audio);
int doom_audio_weapon_volume(DoomAudio *audio, float volume);
void doom_audio_poll(DoomAudio *audio);
void doom_audio_stop_effects(DoomAudio *audio);
/* Replaces the current looping track only after the new voice is ready. */
int doom_audio_music_pcm(DoomAudio *audio, const int16_t *stereo,
                         size_t frames, unsigned rate, float volume);
/* Takes malloc-owned stereo only on success; caller retains it on failure. */
int doom_audio_music_adopt(DoomAudio *audio, int16_t *stereo,
                          size_t frames, unsigned rate, float volume);
void doom_audio_music_stop(DoomAudio *audio);
int doom_audio_music_volume(DoomAudio *audio, float volume);
int doom_audio_pause(DoomAudio *audio, int paused);
#ifdef __cplusplus
}
#endif
#endif
