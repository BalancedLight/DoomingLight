/* Compile the unmodified TinySoundFont implementation separately. */
#define TSF_NO_STDIO
#define TSF_IMPLEMENTATION
#include "../third_party/tsf/tsf.h"

/* TSF's expanded generator offsets can cross sample boundaries even when
 * individual SF2 headers are in range. Check before rendering any voice. */
int doom_tsf_validate_regions(const tsf *font, unsigned sample_count)
{
    int i,j;
    if(!font || !sample_count || font->presetNum<=0 || !font->presets || !font->fontSamples) return 0;
    for(i=0;i<font->presetNum;i++) {
        const struct tsf_preset *p=&font->presets[i];
        if(p->regionNum<0 || (p->regionNum && !p->regions)) return 0;
        for(j=0;j<p->regionNum;j++) {
            const struct tsf_region *r=&p->regions[j];
            if(r->offset>=sample_count || r->end>=sample_count || r->offset>=r->end ||
               r->loop_start>=sample_count || r->loop_end>=sample_count) return 0;
        }
    }
    return 1;
}
