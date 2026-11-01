#ifndef DOOM_WORLD_H
#define DOOM_WORLD_H
#include "doom_core.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/* 32 Doom units per metre maps Doom's 56-unit player height to 1.75 m.
   x/y are horizontal, z is up. Hosts perform the coordinate conversion;
   all distances and speeds below use Doom units. */
#define DOOM_WORLD_UNITS_PER_METRE 32.0f
#define DOOM_WORLD_MAX_PROJECTILES 64
#define DOOM_WORLD_MAX_PICKUPS 64
#define DOOM_WORLD_MAX_SPLASH_TARGETS 64
#define DOOM_WORLD_MAX_BFG_HIT_FX 40
#define DOOM_WORLD_MAX_HIT_FX 64
#define DOOM_WORLD_MAX_IMPACT_SOUNDS 128
#define DOOM_SPLASH_HISTORY 8
#define DOOM_SPLASH_SAMPLE_LIMIT 16
#define DOOM_WORLD_ERROR_HOST_QUERY 1u
#define DOOM_WORLD_ERROR_HOST_DAMAGE 2u

    typedef struct DoomVec3
    {
        float x, y, z;
    } DoomVec3;
    typedef struct DoomTrace
    {
        int hit;
        DoomVec3 point;
        uint64_t target;
    } DoomTrace;
    typedef struct DoomTarget
    {
        uint64_t handle;
        DoomVec3 position;
        float radius;
    } DoomTarget;
    typedef struct DoomWorldHost
    {
        void *context;
        /* Return 1 for a completed query (including misses), 0 on failure. A hit
           with target=0 is world geometry. Never retain a target across ticks. */
        int (*sweep)(void *, DoomVec3 from, DoomVec3 to, float radius, DoomTrace *);
        int (*aim)(void *, DoomVec3 origin, DoomVec3 direction, float range, DoomTrace *);
        int (*nearby)(void *, DoomVec3 origin, float radius, DoomTarget *out, unsigned capacity,
                      unsigned *count);
        /* 1 visible, 0 blocked, negative on unsupported or stale target. */
        int (*visible)(void *, DoomVec3 from, uint64_t target);
        /* Return 1 only when damage was applied; 0 denotes unsupported/stale. */
        int (*damage)(void *, uint64_t target, int amount, DoomWeapon weapon);
        /* Geometry ray without the combat actor-box approximation. */
        int (*ground)(void *, DoomVec3 from, DoomVec3 to, DoomTrace *);
        void *self_context;
        /* Process local-player splash with host line of sight and damage ownership.
           Return 1 for a completed check, including a blocked or invulnerable player. */
        int (*self_splash)(void *, DoomVec3 origin, int amount);
        int self_push;
        DoomVec3 self_center;
        float self_radius;
        int self_bounds_valid;
    } DoomWorldHost;
    typedef enum DoomProjectileState
    {
        DOOM_PROJECTILE_FREE,
        DOOM_PROJECTILE_FLYING,
        DOOM_PROJECTILE_IMPACT
    } DoomProjectileState;
    typedef struct DoomProjectile
    {
        DoomProjectileState state;
        DoomWeapon weapon;
        DoomVec3 position, direction, origin;
        unsigned age, impact_age;
        int frame, spray_done;
    } DoomProjectile;
    typedef struct DoomPickup
    {
        int active;
        DoomDrop kind;
        DoomVec3 position;
        unsigned age;
        int grounded;
    } DoomPickup;
    typedef struct DoomBfgHitFx
    {
        int active;
        unsigned age;
        DoomVec3 position;
    } DoomBfgHitFx;
    typedef enum DoomImpactKind { DOOM_IMPACT_PUFF, DOOM_IMPACT_BLOOD } DoomImpactKind;
    typedef struct DoomHitFx
    {
        int active;
        unsigned age;
        DoomImpactKind kind;
        DoomVec3 position;
    } DoomHitFx;
    typedef struct DoomImpactSound
    {
        DoomWeapon weapon;
        DoomVec3 position;
        const char *lump;
    } DoomImpactSound;
#if defined(DOOM_MODE_LAB)
    typedef enum DoomSplashSampleState
    {
        DOOM_SPLASH_INVALID,
        DOOM_SPLASH_OUTSIDE,
        DOOM_SPLASH_BLOCKED,
        DOOM_SPLASH_QUERY_FAILED,
        DOOM_SPLASH_DAMAGE_FAILED,
        DOOM_SPLASH_DAMAGED
    } DoomSplashSampleState;
    typedef struct DoomSplashSample
    {
        uint64_t target;
        float center_distance, edge_distance, radius;
        int damage;
        DoomSplashSampleState state;
    } DoomSplashSample;
    typedef struct DoomSplashTelemetry
    {
        uint32_t sequence;
        DoomVec3 impact;
        uint64_t direct_target;
        int direct_damage, direct_result;
        unsigned query_ok, candidates, at_capacity, invalid, outside, blocked;
        unsigned visible, query_failed, damage_failed, damaged, sample_count;
        DoomSplashSample samples[DOOM_SPLASH_SAMPLE_LIMIT];
    } DoomSplashTelemetry;
#endif
    typedef struct DoomWorld
    {
        uint64_t generation;
        unsigned errors;
        DoomProjectile projectiles[DOOM_WORLD_MAX_PROJECTILES];
        DoomPickup pickups[DOOM_WORLD_MAX_PICKUPS];
        DoomBfgHitFx bfg_hit_fx[DOOM_WORLD_MAX_BFG_HIT_FX];
        DoomHitFx hit_fx[DOOM_WORLD_MAX_HIT_FX];
        DoomImpactSound impact_sounds[DOOM_WORLD_MAX_IMPACT_SOUNDS];
        unsigned impact_sound_head, impact_sound_count;
        uint64_t killed[128];
        unsigned killed_next;
#if defined(DOOM_MODE_LAB)
        uint32_t splash_sequence;
        DoomSplashTelemetry splash_history[DOOM_SPLASH_HISTORY];
#endif
    } DoomWorld;
    typedef void (*DoomPickupFn)(void *context, DoomDrop kind, int amount);
    void doom_world_reset(DoomWorld *world, uint64_t generation);
    unsigned doom_world_drain_impact_sounds(DoomWorld *world, DoomImpactSound *out, unsigned capacity);
    void doom_world_add_hit_fx(DoomWorld *world, DoomImpactKind kind, DoomVec3 position);
    const char *doom_world_hit_fx_sprite(const DoomHitFx *effect);
    int doom_world_fire(DoomWorld *world, DoomWeapon weapon, DoomVec3 origin, DoomVec3 direction);
    int doom_world_enemy_killed(DoomWorld *world, DoomPlayer *player, uint64_t death_id,
                                DoomVec3 position);
    int doom_world_spawn_pickup(DoomWorld *world, DoomDrop kind, DoomVec3 position);
    /* One simulation step is one Doom tic. A failed collision query removes that
       projectile; it must never pass through geometry on an unknown result. */
    void doom_world_tick(DoomWorld *world, DoomPlayer *player, DoomVec3 player_position,
                         float player_height, const DoomWorldHost *host);
    void doom_world_tick_with_pickups(DoomWorld *world, DoomPlayer *player, DoomVec3 player_position,
                                      float player_height, const DoomWorldHost *host,
                                      DoomPickupFn pickup, void *context);
    const char *doom_world_pickup_sprite(DoomDrop kind);
    /* The age is measured in Doom's 35 Hz tics. */
    const char *doom_world_pickup_sprite_at(DoomDrop kind, unsigned age);
    const char *doom_world_projectile_sprite(const DoomProjectile *projectile);
    const char *doom_world_bfg_hit_sprite(const DoomBfgHitFx *effect);
#ifdef __cplusplus
}
#endif
#endif
