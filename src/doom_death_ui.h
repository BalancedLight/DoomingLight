#ifndef DOOM_DEATH_UI_H
#define DOOM_DEATH_UI_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*DoomDeathUiAfterUpdate)(void *context);

/* Call only after the exact supported Player profile has been verified.
 * A negative result means a partially installed hook needs stop retries. */
int doom_death_ui_start(void *game, DoomDeathUiAfterUpdate after_update, void *context);
/* Returns zero if an already-entered callback must drain before retrying. */
int doom_death_ui_stop(void);

#ifdef __cplusplus
}
#endif
#endif
