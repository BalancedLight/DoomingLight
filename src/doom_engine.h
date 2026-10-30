#ifndef DOOM_ENGINE_H
#define DOOM_ENGINE_H

#include <stdint.h>
#include "doom_core.h"
#include "doom_world.h"
#include "doom_world_visual_flags.h"
#include "doom_damage_direction.h"
#include "doom_movement_sampler.h"
#include "doom_deaths.h"
#include "doom_combat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DoomEngine DoomEngine;
enum DoomEngineCapability {
    DOOM_ENGINE_INPUT = 1u << 0,
    DOOM_ENGINE_MOVEMENT = 1u << 1,
    DOOM_ENGINE_ATTACK = 1u << 2,
    DOOM_ENGINE_DAMAGE = 1u << 3,
    DOOM_ENGINE_DEATHS = 1u << 4,
    DOOM_ENGINE_INTERACT = 1u << 5,
    DOOM_ENGINE_PICKUPS = 1u << 6,
    DOOM_ENGINE_HUD = 1u << 7,
    DOOM_ENGINE_REQUIRED = (1u << 8) - 1u
};

typedef struct DoomEngineFrame {
    uint32_t map_generation;
    int ready, paused, alive, respawn, fire, interact_failed;
    int doom_dead, death_commit, parkour;
    unsigned move_state, edge_state;
    int edge_active, ladder_active;
    unsigned damage_sequence;
    int damage_side, damage_amount, body_suppression;
    unsigned damage_thread, update_thread;
    DoomDamageDirectionSnapshot direction;
    int weapon_select;
    int weapon_slot;
    unsigned cheat_count;
    char cheat_chars[16];
} DoomEngineFrame;

DoomEngine *doom_engine_create(void);
void doom_engine_destroy(DoomEngine *engine);
uint32_t doom_engine_capabilities(const DoomEngine *engine);
/* Bracket loader-owned game-update and scope-deactivation callbacks. */
int doom_engine_begin_update(DoomEngine *engine);
void doom_engine_end_update(DoomEngine *engine);
/* Borrowed for the current game-thread frame only; reacquire after transitions. */
void *doom_engine_level(DoomEngine *engine);
int doom_engine_position(DoomEngine *engine, float *x, float *y, float *z);
int doom_engine_camera_position(DoomEngine *engine, float *x, float *y, float *z);
/* Both outputs use Doom coordinates; valid only during this game update. */
int doom_engine_shot_basis(DoomEngine *engine, DoomVec3 *origin, DoomVec3 *direction);
/* Lab candidate availability; individual host operations can still fail. */
int doom_engine_lab_projectile_ready(DoomEngine *engine, DoomWeapon weapon);
unsigned doom_engine_death_drain(DoomEngine *engine, DoomDeathRecord *out, unsigned capacity);
void doom_engine_bind(DoomEngine *engine, DoomPlayer *player, DoomEventFn emit, void *context);
void doom_engine_reset_damage(DoomEngine *engine);
void doom_engine_configure(DoomEngine *engine, int classic, float incoming, float outgoing);
void doom_engine_set_texture_filtering(DoomEngine *engine, int bilinear);
int doom_engine_texture_filtering(DoomEngine *engine);
void doom_engine_set_dynamic_lights(DoomEngine *engine, int enabled);
int doom_engine_dynamic_lights(DoomEngine *engine);
void doom_engine_set_fire_key(DoomEngine *engine, int virtual_key);
void doom_engine_set_cheats_enabled(DoomEngine *engine, int enabled);
void doom_engine_set_voice_replacement(DoomEngine *engine, int ready);
int doom_engine_initialize(DoomEngine *engine);
int doom_engine_activate(DoomEngine *engine, uint32_t map_generation);
void doom_engine_deactivate(DoomEngine *engine);
int doom_engine_poll(DoomEngine *engine, DoomEngineFrame *frame);
int doom_engine_attack(DoomEngine *engine, const DoomEvent *event);
int doom_engine_report_shot(DoomEngine *engine, DoomWeapon weapon);
int doom_engine_sample_movement(DoomEngine *engine, DoomMovementSnapshot *sample);
const char *doom_engine_attack_reason(DoomEngine *engine);
void doom_engine_attack_counts(DoomEngine *engine, unsigned *actors, unsigned *tested, unsigned *hit);
int doom_engine_damage_observation(DoomEngine *engine, DoomDamageObservation *out);
unsigned doom_engine_drain_damage_observations(DoomEngine *engine, DoomDamageObservation *out, unsigned capacity);
unsigned doom_engine_drain_impacts(DoomEngine *engine, DoomHitscanImpact *out, unsigned capacity);
/* Bind and unbind within the same scoped game update. */
int doom_engine_world_bind(DoomEngine *engine, DoomWorldHost *host);
void doom_engine_world_unbind(DoomEngine *engine);
/* Optional Lab world sprites. Positions use Doom coordinates; size is metres.
 * Calls require the currently validated player/level and game-update scope. */
int doom_engine_world_sprites_open(DoomEngine *engine);
uint32_t doom_engine_world_sprite_add(DoomEngine *engine, const uint8_t *rgba,
                                      unsigned edge, DoomVec3 position, float size_metres, unsigned flags);
int doom_engine_world_sprite_update(DoomEngine *engine, uint32_t token,
                                    const uint8_t *rgba, unsigned edge);
int doom_engine_world_sprite_move(DoomEngine *engine, uint32_t token, DoomVec3 position);
int doom_engine_world_sprite_remove(DoomEngine *engine, uint32_t token);
int doom_engine_world_sprites_close(DoomEngine *engine);
int doom_engine_present(DoomEngine *engine, const uint8_t *rgba, unsigned width,
                        unsigned height, float hud_scale);
int doom_engine_present_layers(DoomEngine *engine, const uint8_t *weapon,
                                const uint8_t *status, const uint8_t *message,
                               float hud_scale, float weapon_scale, float weapon_x, float weapon_y);

#ifdef __cplusplus
}
#endif
#endif
