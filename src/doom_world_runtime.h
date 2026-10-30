#ifndef DOOM_WORLD_RUNTIME_H
#define DOOM_WORLD_RUNTIME_H
#include "doom_world.h"
#ifdef __cplusplus
extern "C"
{
#endif
#define DOOM_WORLD_RUNTIME_UNSUPPORTED_PROJECTILE 1u
#define DOOM_WORLD_RUNTIME_PROJECTILE_CAPACITY 2u
#define DOOM_WORLD_RUNTIME_INVALID_ORIGIN 4u
#define DOOM_WORLD_RUNTIME_INVALID_PLAYER 8u

    typedef struct DoomWorldRuntime
    {
        DoomWorld world;
        unsigned accumulator;
        unsigned flags;
    } DoomWorldRuntime;
    typedef struct DoomWorldFrame
    {
        DoomVec3 player_position, shot_origin, shot_direction;
        float player_height;
        const DoomWorldHost *host; /* Borrowed for this call only. */
        /* If world_active is false, player ticks continue, projectiles are
           rejected without ammo use, and world objects are not advanced. */
        int fire, projectile_supported, world_active;
    } DoomWorldFrame;
    typedef void (*DoomPickupFn)(void *context, DoomDrop kind);
    void doom_world_runtime_reset(DoomWorldRuntime *runtime, uint64_t generation);
    int doom_world_runtime_death(DoomWorldRuntime *runtime, DoomPlayer *player, uint64_t death_id,
                                 DoomVec3 position);
    /* Both the player and world advance on the same 35 Hz tick. The supplied host
       and callbacks must remain valid only until this call returns. A missing
       projectile capability blocks the trigger and preserves ammunition. */
    void doom_world_runtime_advance(DoomWorldRuntime *runtime, DoomPlayer *player,
                                    unsigned elapsed_ms, const DoomWorldFrame *frame,
                                    DoomEventFn emit, DoomPickupFn pickup, void *context);
#ifdef __cplusplus
}
#endif
#endif
