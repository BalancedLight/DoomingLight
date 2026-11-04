#ifndef DOOM_WAD_DISCOVERY_H
#define DOOM_WAD_DISCOVERY_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Manual paths take precedence. game: 1 for Doom, 2 for Doom II. */
int doom_find_iwad(int game, const char *manual_path, char *out, size_t capacity);
/* game: 0 automatic, 1 Doom, 2 Doom II, 3 Freedoom Phase 1, 4 Phase 2.
 * Explicit Freedoom choices ignore retail paths and the fallback toggle.
 * The output flag reports whether bundled Freedoom was selected. */
int doom_select_iwad(int game, const char *doom_path, const char *doom2_path,
                     int allow_freedoom, const char *bundle_directory,
                     char *out, size_t capacity, int *used_freedoom);
#ifdef __cplusplus
}
#endif
#endif
