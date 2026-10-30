#ifndef DOOM_VIEWMODEL_H
#define DOOM_VIEWMODEL_H
#ifdef __cplusplus
extern "C" {
#endif
/* Caller verifies the exact Player profile before installation. Only the
 * current local first-person visual and its weapon models are affected. */
int doom_viewmodel_start(void *game_module, void *engine_module);
/* Disable the hook immediately; restoration remains pending until stop. */
int doom_viewmodel_suspend(void);
/* Stop must run on a game update. Zero means the caller must retry cleanup. */
int doom_viewmodel_stop(void);
#ifdef __cplusplus
}
#endif
#endif
