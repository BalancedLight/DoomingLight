#ifndef DOOM_HOSTILITY_H
#define DOOM_HOSTILITY_H
#ifdef __cplusplus
extern "C" {
#endif
/* Lab build only: returns 1 for hostile, 0 for non-hostile, -1 for unknown.
 * The caller must identify an approved game DLL by SHA-256, verify that player
 * is the current local player, and revalidate both objects against their live
 * level and object IDs on the game thread immediately before this call. */
int doom_hostility_query(void *verified_game_module, void *target_object,
                         void *local_player_object);
#ifdef __cplusplus
}
#endif
#endif
