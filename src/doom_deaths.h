#ifndef DOOM_DEATHS_H
#define DOOM_DEATHS_H
#include <stdint.h>
#include "doom_world.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomDeaths DoomDeaths;
typedef struct DoomDeathRecord { uint64_t id; DoomVec3 position; } DoomDeathRecord;
/* Caller must verify exact loaded Player profile before creation. */
DoomDeaths *doom_deaths_create(void *game_module, void *engine_module);
int doom_deaths_start(DoomDeaths *observer);
/* Call on each map change and whenever player/level ownership changes. */
void doom_deaths_context(DoomDeaths *observer, void *player, void *level, uint32_t generation);
/* Revalidates object IDs, player/level ownership, and hostility on the game update. */
unsigned doom_deaths_drain(DoomDeaths *observer, DoomDeathRecord *out, unsigned capacity);
/* Returns zero if the hook could not be disabled; retry before restarting. */
int doom_deaths_stop(DoomDeaths *observer);
void doom_deaths_destroy(DoomDeaths *observer);
#ifdef __cplusplus
}
#endif
#endif
