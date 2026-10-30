#ifndef DOOM_COMBAT_H
#define DOOM_COMBAT_H
#include "doom_core.h"
#include "doom_world.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomCombat DoomCombat;
typedef struct DoomHitscanImpact { DoomImpactKind kind; DoomVec3 position; } DoomHitscanImpact;
typedef struct DoomDamageObservation {
    uint32_t sequence;
    int weapon, target_id, before_valid, after_valid, removed;
    float native_damage, physics_damage, before, after;
} DoomDamageObservation;
DoomCombat *doom_combat_create(void *game_module, void *engine_module);
void doom_combat_destroy(DoomCombat *combat);
/* Called on the Player game thread with freshly acquired objects. */
int doom_combat_hitscan(DoomCombat *combat, void *player, void *level,
                        void *camera, const DoomEvent *event, float damage_scale);
const char *doom_combat_last_reason(const DoomCombat *combat);
unsigned doom_combat_actor_count(const DoomCombat *combat);
unsigned doom_combat_boxes_tested(const DoomCombat *combat);
unsigned doom_combat_boxes_hit(const DoomCombat *combat);
int doom_combat_last_observation(const DoomCombat *combat, DoomDamageObservation *out);
unsigned doom_combat_drain_observations(DoomCombat *combat, DoomDamageObservation *out, unsigned capacity);
unsigned doom_combat_drain_impacts(DoomCombat *combat, DoomHitscanImpact *out, unsigned capacity);
/* Host is valid only until unbind; rebuild once per game-thread world tick. */
int doom_combat_world_bind(DoomCombat *combat, void *player, void *level,
                           uint32_t generation, float damage_scale, DoomWorldHost *host);
void doom_combat_world_unbind(DoomCombat *combat);
#ifdef __cplusplus
}
#endif
#endif
