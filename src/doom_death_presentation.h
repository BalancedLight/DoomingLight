#ifndef DOOM_DEATH_PRESENTATION_H
#define DOOM_DEATH_PRESENTATION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Called only after the caller has verified the exact Player profile. */
int doom_death_presentation_start(void *game, void *engine);
/* camera and actor are borrowed for this map generation; pass dead_wait=0
 * before committing native death, on pause, and before changing maps. */
void doom_death_presentation_set(void *camera, void *actor, uint32_t generation,
                                 int dead_wait, int view_height);
/* Call in a validated game update; zero means restoration must be retried. */
int doom_death_presentation_stop(void);
#ifdef __cplusplus
}
#endif
#endif
