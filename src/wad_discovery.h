#ifndef DOOM_WAD_DISCOVERY_H
#define DOOM_WAD_DISCOVERY_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Manual paths take precedence. game: 1 for Doom, 2 for Doom II. */
int doom_find_iwad(int game, const char *manual_path, char *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
