#ifndef DOOM_MENU_GUARD_H
#define DOOM_MENU_GUARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* The caller selects the independently validated Player profile. */
int doom_menu_guard_start(void *game_module, int supported_profile);
int doom_menu_guard_stop(void);

#ifdef __cplusplus
}
#endif
#endif
