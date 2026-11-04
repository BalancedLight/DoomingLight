/* Compile the unmodified TinySoundFont implementation separately. */
#define TSF_NO_STDIO
#define TSF_IMPLEMENTATION
#include "../third_party/tsf/tsf.h"

/* TSF's expanded generator offsets can cross sample boundaries even when
 * individual SF2 headers are in range. Check before rendering any voice and
 * constrain loops to the region after all generator offsets are applied. */
int doom_tsf_prepare_regions(tsf *font, unsigned sample_count)
{
    int i,j;
    if(!font || !sample_count || font->presetNum<=0 || !font->presets || !font->fontSamples) return 0;
    for(i=0;i<font->presetNum;i++) {
        struct tsf_preset *p=&font->presets[i];
        if(p->regionNum<0 || (p->regionNum && !p->regions)) return 0;
        for(j=0;j<p->regionNum;j++) {
            struct tsf_region *r=&p->regions[j];
            if(r->offset>=sample_count || r->end>=sample_count || r->offset>=r->end) return 0;
            if(r->loop_start<r->offset) r->loop_start=r->offset;
            if(r->loop_end>=r->end) r->loop_end=r->end-1;
            if(r->loop_start>=r->loop_end) {
                r->loop_mode=TSF_LOOPMODE_NONE;
                r->loop_start=r->loop_end=0;
            }
        }
    }
    return 1;
}
