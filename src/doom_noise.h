#ifndef DOOM_NOISE_H
#define DOOM_NOISE_H

#include "doom_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DoomNoise DoomNoise;

/* Caller must validate the paired Player modules and use a serialized game
 * update. Actor and level are borrowed only for this call. */
DoomNoise *doom_noise_create(void *game_module, void *engine_module);
void doom_noise_destroy(DoomNoise *noise);

/* Returns 1 when reported, 0 when intentionally skipped, -1 on failure.
 * Chainsaw reports are rate limited. */
int doom_noise_report(DoomNoise *noise, void *actor, void *level,
                      DoomWeapon weapon);

#ifdef __cplusplus
}
#endif
#endif
