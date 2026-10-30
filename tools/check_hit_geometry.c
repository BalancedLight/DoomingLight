#include "doom_hit_geometry.h"
#include <stdio.h>

int main(void)
{
    const float box[6]={4,-1,-1,6,1,1};
    const float start[3]={0,0,0}, end[3]={10,0,0}, wall[3]={3,0,0};
    const float outside[3]={10,2,0}, inside[3]={5,0,0};
    float t=-1;
    if (!doom_segment_box(start,end,box,0,&t) || fabsf(t-0.4f)>0.0001f) return 1;
    if (doom_segment_box(start,wall,box,0,&t)) return 2;
    if (doom_segment_box(outside,end,box,0,&t)) return 3;
    if (!doom_segment_box(inside,end,box,0,&t) || t!=0.0f) return 4;
    if (!doom_segment_box(start,wall,box,1.0f,&t) || fabsf(t-1.0f)>0.0001f) return 5;
    puts("Doom hit geometry checks passed.");
    return 0;
}
