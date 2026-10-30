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
    } DoomPickup;
    typedef struct DoomWorld
    {
        uint64_t generation;
        unsigned errors;
        DoomProjectile projectiles[DOOM_WORLD_MAX_PROJECTILES];
        DoomPickup pickups[DOOM_WORLD_MAX_PICKUPS];
        uint64_t killed[128];
        unsigned killed_next;
    } DoomWorld;
    typedef void (*DoomPickupFn)(void *context, DoomDrop kind, int amount);
    void doom_world_reset(DoomWorld *world, uint64_t generation);
    int doom_world_fire(DoomWorld *world, DoomWeapon weapon, DoomVec3 origin, DoomVec3 direction);
    int doom_world_enemy_killed(DoomWorld *world, DoomPlayer *player, uint64_t death_id,
                                DoomVec3 position);
    /* One simulation step is one Doom tic. A failed collision query removes that
       projectile; it must never pass through geometry on an unknown result. */
    void doom_world_tick(DoomWorld *world, DoomPlayer *player, DoomVec3 player_position,
                         float player_height, const DoomWorldHost *host);
    void doom_world_tick_with_pickups(DoomWorld *world, DoomPlayer *player, DoomVec3 player_position,
                                      float player_height, const DoomWorldHost *host,
                                      DoomPickupFn pickup, void *context);
    const char *doom_world_pickup_sprite(DoomDrop kind);
    const char *doom_world_projectile_sprite(const DoomProjectile *projectile);
#ifdef __cplusplus
}
#endif
#endif
