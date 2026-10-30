#ifndef DOOM_FPP_BODY_H
#define DOOM_FPP_BODY_H
#ifdef __cplusplus
extern "C" {
#endif
/* All calls run on the game thread. The caller suspends suppression for menus
 * and scripted cameras, and calls stop before the game modules are unloaded. */
int doom_fpp_body_start(void *game_module, void *engine_module);
int doom_fpp_body_update(int active);
int doom_fpp_body_stop(void);
#ifdef __cplusplus
}
#endif
#endif
