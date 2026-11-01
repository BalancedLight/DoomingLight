#ifndef DOOM_DEATH_PRESENTATION_H
#define DOOM_DEATH_PRESENTATION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Called only after the caller has verified the exact Player profile. */
int doom_death_presentation_start(void *game, void *engine);
/* Camera and actor are borrowed for this map generation. Keep the view active
 * while the native death fade runs, then clear it on respawn or map teardown. */
void doom_death_presentation_set(void *camera, void *actor, uint32_t generation,
                                 int dead_wait, int view_height);
/* Hold the most recent lowered view while native death and fade proceed. */
void doom_death_presentation_commit(void);
/* Call in a validated game update; zero means restoration must be retried. */
int doom_death_presentation_stop(void);
#ifdef __cplusplus
}
#endif
#endif
