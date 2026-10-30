#ifndef DOOM_WORLD_DISPLAY_H
#define DOOM_WORLD_DISPLAY_H
#include "doom_engine.h"
#include "doom_wad.h"
#include "doom_world.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomWorldDisplay DoomWorldDisplay;
/* All calls are scoped to the current engine update. The display borrows its
 * WAD only during update; the engine retains native resources until close. */
DoomWorldDisplay *doom_world_display_create(void);
int doom_world_display_update(DoomWorldDisplay *, DoomEngine *, const DoomWad *,
                              const DoomWorld *, uint32_t generation);
int doom_world_display_close(DoomWorldDisplay *, DoomEngine *);
void doom_world_display_destroy(DoomWorldDisplay *);
#ifdef __cplusplus
}
#endif
#endif
