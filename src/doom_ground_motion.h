#ifndef DOOM_GROUND_MOTION_H
#define DOOM_GROUND_MOTION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /* Doom fixed-point momentum in world-horizontal Doom units per 35 Hz tic. */
    typedef struct DoomGroundMotion
    {
        int32_t momentum_x, momentum_z;
    } DoomGroundMotion;
    typedef struct DoomGroundInput
    {
        /* Unit horizontal forward direction in native world x/z axes. Positive
           strafe is to the player's right. Inputs are normalized to [-1, 1]. */
        float forward_x, forward_z, forward, strafe;
        int run;
    } DoomGroundInput;
    typedef struct DoomGroundVelocity
    {
        float x_mps, z_mps;
    } DoomGroundVelocity;
    void doom_ground_motion_reset(DoomGroundMotion *motion);
    /* Call exactly once per grounded 35 Hz tic. The output is the pre-friction
     * horizontal target in metres/second, based on 32 Doom units/metre. A caller
     * must suspend/reset on map, player, air and parkour transitions. This pure
     * simulation does not set native physics velocity or perform collision. */
    int doom_ground_motion_tick(DoomGroundMotion *motion, const DoomGroundInput *input,
                                DoomGroundVelocity *velocity);
#ifdef __cplusplus
}
#endif
#endif
