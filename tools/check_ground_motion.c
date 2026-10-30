#include "doom_ground_motion.h"
#include <assert.h>
#include <math.h>
int main(void)
{
    DoomGroundMotion motion={0}; DoomGroundInput input={0,1,1,0,0}; DoomGroundVelocity out={0};
    unsigned i;
    assert(doom_ground_motion_tick(&motion,&input,&out));
    assert(fabsf(out.z_mps-0.8544921875f)<0.00001f && out.x_mps==0);
    for(i=0;i<200;i++) assert(doom_ground_motion_tick(&motion,&input,&out));
    assert(fabsf(out.z_mps-9.114583f)<0.001f);
    doom_ground_motion_reset(&motion); input.run=1;
    for(i=0;i<200;i++) assert(doom_ground_motion_tick(&motion,&input,&out));
    assert(fabsf(out.z_mps-18.229167f)<0.001f);
    input.forward=0; input.strafe=1; doom_ground_motion_reset(&motion);
    for(i=0;i<200;i++) assert(doom_ground_motion_tick(&motion,&input,&out));
    assert(fabsf(out.x_mps-14.583333f)<0.001f && out.z_mps==0);
    input.strafe=0;
    for(i=0;i<120;i++) assert(doom_ground_motion_tick(&motion,&input,&out));
    assert(motion.momentum_x==0 && motion.momentum_z==0);
    input.forward_x=NAN;
    assert(!doom_ground_motion_tick(&motion,&input,&out));
    assert(motion.momentum_x==0 && motion.momentum_z==0 && out.x_mps==0 && out.z_mps==0);
    return 0;
}
