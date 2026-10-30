#ifndef DOOM_MOVEMENT_SAMPLER_H
#define DOOM_MOVEMENT_SAMPLER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /* Lab diagnostics only. Identities are transient values for private logs;
     * they are never pointers for the caller to dereference or show in the UI. */
    typedef struct DoomMovementSampler DoomMovementSampler;
    typedef enum DoomMovementSamplerError
    {
        DOOM_SAMPLER_OK = 0,
        DOOM_SAMPLER_ARGUMENT,
        DOOM_SAMPLER_ACTOR,
        DOOM_SAMPLER_ACTOR_ID,
        DOOM_SAMPLER_CONTROLLER,
        DOOM_SAMPLER_BIND,
        DOOM_SAMPLER_PHYSICS,
        DOOM_SAMPLER_GETTERS,
        DOOM_SAMPLER_VALUES,
        DOOM_SAMPLER_CHANGED,
        DOOM_SAMPLER_ACCESS
    } DoomMovementSamplerError;
    typedef struct DoomMovementSnapshot
    {
        uint64_t qpc, actor_identity, controller_identity, physics_identity;
        uint32_t generation, thread_id, move_state;
        int actor_id, in_air, parkour;
        float position[3], wanted_world_speed[3], walk_acceleration, walk_deceleration;
    } DoomMovementSnapshot;
    /* The caller must first validate the exact CBC7+A18 loaded profile. This helper
     * additionally checks the module hashes. It installs no hooks or patches. */
    DoomMovementSampler *doom_movement_sampler_create(void *game_module, void *engine_module,
                                                      int exact_profile_verified);
    /* Call only from the current owning gameplay update thread. Position, time and
     * move state are caller-copied; each sample revalidates the borrowed actor,
     * controller, level, and walk physics chain before and after the getters.
     * Physics fields can change between getter reads; values are not atomic. */
    int doom_movement_sampler_read(DoomMovementSampler *sampler, void *actor, void *level,
                                   uint32_t generation, uint32_t game_thread_id, uint64_t qpc,
                                   const float position[3], uint32_t move_state, int parkour,
                                   DoomMovementSnapshot *out);
    DoomMovementSamplerError doom_movement_sampler_last_error(const DoomMovementSampler *sampler);
    void doom_movement_sampler_destroy(DoomMovementSampler *sampler);
#ifdef __cplusplus
}
#endif
#endif
