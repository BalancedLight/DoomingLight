#ifndef DOOM_WAD_H
#define DOOM_WAD_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomWad DoomWad;
typedef struct DoomLump { const uint8_t *data; size_t size; } DoomLump;
typedef struct DoomImage { uint32_t width, height; int16_t left, top; uint8_t *rgba; } DoomImage;
typedef struct DoomSound { uint32_t rate, count; int16_t *samples; } DoomSound;
/* Paths are UTF-8. Pointers returned by doom_wad_lump live until close. */
DoomWad *doom_wad_open(const char *path);
void doom_wad_close(DoomWad *wad);
int doom_wad_is_doom2(const DoomWad *wad);
int doom_wad_lump(const DoomWad *wad, const char *name, DoomLump *out);
int doom_wad_patch(const DoomWad *wad, const char *name, DoomImage *out);
void doom_image_free(DoomImage *image);
int doom_wad_sound(const DoomWad *wad, const char *name, DoomSound *out);
void doom_sound_free(DoomSound *sound);
#ifdef __cplusplus
}
#endif
#endif
