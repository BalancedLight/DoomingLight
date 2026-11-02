#ifndef DOOM_SOUND_FILE_H
#define DOOM_SOUND_FILE_H
#include "doom_wad.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Reads a local PCM16 WAV and returns malloc-owned mono samples. */
int doom_sound_file_load(const char *utf8_path, DoomSound *out);
#ifdef __cplusplus
}
#endif
#endif
