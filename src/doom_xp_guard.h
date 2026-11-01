#ifndef DOOM_XP_GUARD_H
#define DOOM_XP_GUARD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomXpGuard DoomXpGuard;
/* The caller verifies the supported Player build before creating the guard. */
DoomXpGuard *doom_xp_guard_create(void *game_module, void *engine_module);
int doom_xp_guard_start(DoomXpGuard *guard, void *level, uint32_t generation);
void doom_xp_guard_set_active(DoomXpGuard *guard, int active);
int doom_xp_guard_stop(DoomXpGuard *guard);
void doom_xp_guard_destroy(DoomXpGuard *guard);
#ifdef __cplusplus
}
#endif
#endif
