#ifndef DOOM_MODE_MUSIC_H
#define DOOM_MODE_MUSIC_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomMusicEvent {
    uint32_t tick;
    uint8_t status, data1, data2;
} DoomMusicEvent;
/* Returns required count when out is NULL. Returns 0 for malformed data. */
size_t doom_music_parse_mus(const uint8_t *data, size_t size,
                            DoomMusicEvent *out, size_t capacity);
size_t doom_music_parse_midi(const uint8_t *data, size_t size,
                             DoomMusicEvent *out, size_t capacity);
/* Resolve the original IDMUS two-digit code to a WAD lump name. */
int doom_music_lump_name(int doom2, unsigned code, char name[9]);
#ifdef __cplusplus
}
#endif
#endif
