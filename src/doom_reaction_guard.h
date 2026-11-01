#ifndef DOOM_REACTION_GUARD_H
#define DOOM_REACTION_GUARD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct DoomReactionGuard DoomReactionGuard;
/* The caller must verify the exact supported Player module profile. */
DoomReactionGuard *doom_reaction_guard_create(void *game_module, void *engine_module);
/* Calls must be made in the serialized Player update for the current level. */
int doom_reaction_guard_start(DoomReactionGuard *guard, void *level, uint32_t generation);
void doom_reaction_guard_set_active(DoomReactionGuard *guard, int active);
/* A failed stop retains the guard and should be retried before reactivation. */
int doom_reaction_guard_stop(DoomReactionGuard *guard);
void doom_reaction_guard_destroy(DoomReactionGuard *guard);

#ifdef __cplusplus
}
#endif
#endif
