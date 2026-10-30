#ifndef DOOM_INTERACTION_STATE_H
#define DOOM_INTERACTION_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read only during the serialized game update after validating the Player
 * module profile. Returns 1 for an active object interaction, 0 for inactive
 * or rejected interactions, and -1 when the current owner cannot be verified. */
int doom_interaction_state(void *game_module, void *actor);

#ifdef __cplusplus
}
#endif
#endif
