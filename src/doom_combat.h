#ifndef DOOM_COMBAT_H
#define DOOM_COMBAT_H
#include "doom_core.h"
#include "doom_world.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomCombat DoomCombat;
DoomCombat *doom_combat_create(void *game_module, void *engine_module);
void doom_combat_destroy(DoomCombat *combat);
/* Called on the Player game thread with freshly acquired objects. */
int doom_combat_hitscan(DoomCombat *combat, void *player, void *level,
                        void *camera, const DoomEvent *event, float damage_scale);
const char *doom_combat_last_reason(const DoomCombat *combat);
/* Host is valid only until unbind; rebuild once per game-thread world tick. */
int doom_combat_world_bind(DoomCombat *combat, void *player, void *level,
                           uint32_t generation, float damage_scale, DoomWorldHost *host);
void doom_combat_world_unbind(DoomCombat *combat);
#ifdef __cplusplus
}
#endif
#endif
