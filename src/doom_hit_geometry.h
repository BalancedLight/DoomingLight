#ifndef DOOM_HIT_GEOMETRY_H
#define DOOM_HIT_GEOMETRY_H
#include <math.h>

/* Intersect a bounded segment with a world-space axis-aligned box. The
   optional expansion is in the same units as the coordinates. */
static int doom_segment_box(const float from[3], const float to[3],
                            const float bounds[6], float expansion, float *fraction)
{
    float first=0.0f, last=1.0f;
    int i;
    if (!from || !to || !bounds || !fraction || !isfinite(expansion) || expansion<0.0f)
        return 0;
    for (i=0;i<3;i++) {
        float direction=to[i]-from[i], lo=bounds[i]-expansion, hi=bounds[i+3]+expansion;
        float near_value, far_value, swap;
        if (!isfinite(from[i]) || !isfinite(to[i]) || !isfinite(lo) || !isfinite(hi) || lo>hi)
            return 0;
        if (fabsf(direction)<0.000001f) {
            if (from[i]<lo || from[i]>hi) return 0;
            continue;
        }
        near_value=(lo-from[i])/direction; far_value=(hi-from[i])/direction;
        if (near_value>far_value) { swap=near_value; near_value=far_value; far_value=swap; }
        if (near_value>first) first=near_value;
        if (far_value<last) last=far_value;
        if (first>last) return 0;
    }
    *fraction=first;
    return 1;
}
#endif
