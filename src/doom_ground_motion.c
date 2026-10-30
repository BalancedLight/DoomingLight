/* Doom-derived ground movement rules. Copyright (C) 1993-1996 id Software, Inc.
 * Distributed under the GNU General Public License, version 2 or later. */
#include "doom_ground_motion.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
#define FRAC_UNIT 65536
#define MAX_MOMENTUM (30 * FRAC_UNIT)
#define STOP_MOMENTUM 0x1000
#define FRICTION 0xe800
static int32_t clamp_momentum(int64_t value)
{
    if (value > MAX_MOMENTUM)
        return MAX_MOMENTUM;
    if (value < -MAX_MOMENTUM)
        return -MAX_MOMENTUM;
    return (int32_t)value;
}
static float clamp_input(float value)
{
    if (value > 1.0f)
        return 1.0f;
    if (value < -1.0f)
        return -1.0f;
    return value;
}
void doom_ground_motion_reset(DoomGroundMotion *motion)
{
    if (motion)
        memset(motion, 0, sizeof(*motion));
}
int doom_ground_motion_tick(DoomGroundMotion *motion, const DoomGroundInput *input,
                            DoomGroundVelocity *velocity)
{
    double length;
    int32_t fx, fz, forward, strafe;
    int64_t thrust_x, thrust_z;
    if (!motion || !input || !velocity)
        return 0;
    if (!isfinite(input->forward_x) || !isfinite(input->forward_z) || !isfinite(input->forward) ||
        !isfinite(input->strafe))
    {
        doom_ground_motion_reset(motion);
        velocity->x_mps = velocity->z_mps = 0;
        return 0;
    }
    length = hypot((double)input->forward_x, (double)input->forward_z);
    if (!isfinite(length) || length < 0.000001)
    {
        doom_ground_motion_reset(motion);
        velocity->x_mps = velocity->z_mps = 0;
        return 0;
    }
    fx = (int32_t)lround((double)input->forward_x / length * FRAC_UNIT);
    fz = (int32_t)lround((double)input->forward_z / length * FRAC_UNIT);
    forward = (int32_t)lround((double)clamp_input(input->forward) * (input->run ? 50.0 : 25.0));
    strafe = (int32_t)lround((double)clamp_input(input->strafe) * (input->run ? 40.0 : 24.0));
    /* P_Thrust applies each axis separately; preserve its fixed-point
     * rounding before adding the forward and strafe contributions. */
    thrust_x = (((int64_t)forward * 2048 * fx) >> 16) +
               (((int64_t)strafe * 2048 * fz) >> 16);
    thrust_z = (((int64_t)forward * 2048 * fz) >> 16) +
               (((int64_t)strafe * 2048 * -fx) >> 16);
    motion->momentum_x = clamp_momentum((int64_t)motion->momentum_x + thrust_x);
    motion->momentum_z = clamp_momentum((int64_t)motion->momentum_z + thrust_z);
    velocity->x_mps = (float)((double)motion->momentum_x / FRAC_UNIT * 35.0 / 32.0);
    velocity->z_mps = (float)((double)motion->momentum_z / FRAC_UNIT * 35.0 / 32.0);
    if (!forward && !strafe && motion->momentum_x > -STOP_MOMENTUM &&
        motion->momentum_x < STOP_MOMENTUM && motion->momentum_z > -STOP_MOMENTUM &&
        motion->momentum_z < STOP_MOMENTUM)
    {
        doom_ground_motion_reset(motion);
    }
    else
    {
        motion->momentum_x = (int32_t)(((int64_t)motion->momentum_x * FRICTION) >> 16);
        motion->momentum_z = (int32_t)(((int64_t)motion->momentum_z * FRICTION) >> 16);
    }
    return 1;
}
