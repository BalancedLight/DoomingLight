#ifndef DOOM_POWERUPS_CAMO_H
#define DOOM_POWERUPS_CAMO_H
#ifdef __cplusplus
extern "C" {
#endif
/* The caller supplies the currently validated local player on the game thread. */
int doom_powerups_camo_start(void *game_module);
int doom_powerups_camo_update(void *player, int enabled, float seconds);
int doom_powerups_camo_stop(void *player);
/* Only after the owning level begins destruction; never dereferences its player. */
int doom_powerups_camo_forget(void);
#ifdef __cplusplus
}
#endif
#endif
