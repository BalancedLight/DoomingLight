#include "doom_synth.h"
#include "doom_music.h"
#define TSF_NO_STDIO
#include "../third_party/tsf/tsf.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define SF2_MAX_BYTES (256u * 1024u * 1024u)
int doom_tsf_validate_regions(const tsf *font, unsigned sample_count);
typedef struct SfChunk { const uint8_t *data; size_t size; } SfChunk;
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

/* Validate the hydra indices used by TinySoundFont before it dereferences
 * them. The font can be selected from an arbitrary file by the player. */
static int find_chunks(const uint8_t *data,size_t start,size_t end,SfChunk *chunks,int section) {
    static const char *const names[]={"phdr","pbag","pmod","pgen","inst","ibag","imod","igen","shdr"};
    size_t pos=start; unsigned i;
    while(pos<end) {
        uint32_t length; size_t next; int child=section;
        if(end-pos<8) return 0;
        length=rd32(data+pos+4);
        if((size_t)length>end-pos-8) return 0;
        next=pos+8+(size_t)length;
        if(!memcmp(data+pos,"LIST",4)) {
            if(length<4 || section) return 0;
            if(!memcmp(data+pos+8,"pdta",4)) child=1;
            else if(!memcmp(data+pos+8,"sdta",4)) child=2;
            else child=3;
            if(!find_chunks(data,pos+12,next,chunks,child)) return 0;
        } else if(section==1) {
            for(i=0;i<9;i++) if(!memcmp(data+pos,names[i],4)) {
                if(chunks[i].data) return 0;
                chunks[i].data=data+pos+8; chunks[i].size=length;
            }
        } else if(section==2 && !memcmp(data+pos,"smpl",4)) {
            if(chunks[9].data) return 0;
            chunks[9].data=data+pos+8; chunks[9].size=length;
        }
        if(length&1u) { if(next>=end) return 0; next++; }
        pos=next;
    }
    return pos==end;
}
static int sf2_valid(const uint8_t *font,size_t size,unsigned *samples_out) {
    static const unsigned record[]={38,4,10,4,22,4,10,4,46};
    SfChunk c[10]={{0}}; size_t n[9],i,work=0; uint32_t sample_count; unsigned previous;
    if(!font || size<12 || size>SF2_MAX_BYTES || size>(size_t)INT_MAX ||
       memcmp(font,"RIFF",4) || memcmp(font+8,"sfbk",4) || rd32(font+4)!=size-8) return 0;
    if(!find_chunks(font,12,size,c,0) || !c[9].data || c[9].size<4 || (c[9].size&1)) return 0;
    for(i=0;i<9;i++) {
        if(!c[i].data || c[i].size%record[i] || !(n[i]=c[i].size/record[i]) || n[i]>65536) return 0;
    }
    if(n[0]<2 || n[0]>1025 || n[1]<2 || n[3]<1 || n[4]<2 || n[5]<2 || n[7]<1 || n[8]<2) return 0;
    previous=0;
    for(i=0;i<n[0];i++) { unsigned x=rd16(c[0].data+i*38+24); if(x<previous || x>=n[1]) return 0; previous=x; }
    previous=0;
    for(i=0;i<n[4];i++) { unsigned x=rd16(c[4].data+i*22+20); if(x<previous || x>=n[5]) return 0; previous=x; }
    previous=0;
    for(i=0;i<n[1];i++) { unsigned x=rd16(c[1].data+i*4); if(x<previous || x>=n[3] || rd16(c[1].data+i*4+2)>=n[2]) return 0; previous=x; }
    previous=0;
    for(i=0;i<n[5];i++) { unsigned x=rd16(c[5].data+i*4); if(x<previous || x>=n[7] || rd16(c[5].data+i*4+2)>=n[6]) return 0; previous=x; }
    for(i=0;i<n[3];i++) if(rd16(c[3].data+i*4)==41 && rd16(c[3].data+i*4+2)>=n[4]-1) return 0;
    for(i=0;i<n[7];i++) if(rd16(c[7].data+i*4)==53 && rd16(c[7].data+i*4+2)>=n[8]-1) return 0;
    /* Bound expanded preset/instrument zones as well as on-disk record counts. */
    for(i=0;i+1<n[0];i++) {
        unsigned pb,first=rd16(c[0].data+i*38+24),last=rd16(c[0].data+(i+1)*38+24);
        for(pb=first;pb<last;pb++) {
            unsigned pg,pstart=rd16(c[1].data+pb*4),pend=rd16(c[1].data+(pb+1)*4);
            for(pg=pstart;pg<pend;pg++) if(rd16(c[3].data+pg*4)==41) {
                unsigned id=rd16(c[3].data+pg*4+2),ib,begin=rd16(c[4].data+id*22+20),end=rd16(c[4].data+(id+1)*22+20);
                for(ib=begin;ib<end;ib++) {
                    size_t part=rd16(c[5].data+(ib+1)*4)-rd16(c[5].data+ib*4)+1u;
                    if(part>1000000u-work) return 0; work+=part;
                }
            }
        }
    }
    sample_count=(uint32_t)(c[9].size/2);
    for(i=0;i+1<n[8];i++) {
        const uint8_t *s=c[8].data+i*46;
        uint32_t begin=rd32(s+20),end=rd32(s+24),loop_begin=rd32(s+28),loop_end=rd32(s+32),rate=rd32(s+36);
        if(begin>=end || end>=sample_count || !rate || rate>768000 || loop_begin>loop_end || loop_end>end || (loop_begin && loop_begin<begin)) return 0;
    }
    *samples_out=sample_count;
    return 1;
}

static int render_until(tsf *font,int16_t *samples,size_t *position,size_t target,DoomSynthCancelFn cancel,void *context) {
    while(*position<target) {
        size_t n=target-*position; if(n>1024) n=1024;
        if(cancel && cancel(context)) return 0;
        tsf_render_short(font,samples+*position*2,(int)n,0); *position+=n;
    }
    return 1;
}
static int midi_event(tsf *font,DoomMusicEvent e) {
    int channel=e.status&15, kind=e.status&0xf0;
    switch(kind) {
    case 0x80: tsf_channel_note_off(font,channel,e.data1); return 1;
    case 0x90: if(!e.data2) { tsf_channel_note_off(font,channel,e.data1); return 1; }
               return tsf_channel_note_on(font,channel,e.data1,e.data2/127.0f);
    case 0xb0: return tsf_channel_midi_control(font,channel,e.data1,e.data2);
    case 0xc0: if(tsf_channel_set_presetnumber(font,channel,e.data1,channel==9)) return 1;
               if(tsf_channel_set_presetnumber(font,channel,0,channel==9)) return 1;
               return tsf_channel_set_presetindex(font,channel,0);
    case 0xe0: return tsf_channel_set_pitchwheel(font,channel,e.data1|((int)e.data2<<7));
    default: return 1;
    }
}
int doom_synth_render_sf2_cancelable(const uint8_t *music,size_t size,const uint8_t *font_data,size_t font_size,
                                     int16_t **pcm,size_t *frames,unsigned rate,DoomSynthCancelFn cancel,void *context) {
    size_t count,i,pos=0,total; DoomMusicEvent *events=NULL; int16_t *samples=NULL; tsf *font=NULL; int midi; unsigned sample_count=0;
    if(!pcm||!frames) return 0; *pcm=NULL; *frames=0;
    if(!music||size<4||rate<8000||rate>48000||(cancel&&cancel(context))||!sf2_valid(font_data,font_size,&sample_count)) return 0;
    midi=!memcmp(music,"MThd",4);
    count=midi?doom_music_parse_midi(music,size,NULL,0):doom_music_parse_mus(music,size,NULL,0);
    if(!count||count>1000000||(cancel&&cancel(context))) return 0;
    events=(DoomMusicEvent*)malloc(count*sizeof(*events)); if(!events) return 0;
    if((midi?doom_music_parse_midi(music,size,events,count):doom_music_parse_mus(music,size,events,count))!=count||events[count-1].tick>140*600) goto fail;
    if(cancel&&cancel(context)) goto fail;
    font=tsf_load_memory(font_data,(int)font_size); if(!font || !doom_tsf_validate_regions(font,sample_count)) goto fail;
    if(cancel&&cancel(context)) goto fail;
    tsf_set_output(font,TSF_STEREO_INTERLEAVED,(int)rate,-9.0f);
    if(!tsf_set_max_voices(font,256)) goto fail;
    for(i=0;i<16;i++) {
        if((!tsf_channel_set_presetnumber(font,(int)i,0,i==9) && !tsf_channel_set_presetindex(font,(int)i,0)) ||
           !tsf_channel_midi_control(font,(int)i,7,100) ||
           !tsf_channel_midi_control(font,(int)i,11,127)) goto fail;
    }
    total=(size_t)events[count-1].tick*rate/140+rate*2;
    samples=(int16_t*)malloc(total*2*sizeof(*samples)); if(!samples) goto fail;
    for(i=0;i<count;i++) {
        size_t target=(size_t)events[i].tick*rate/140;
        if((cancel&&cancel(context)) || !render_until(font,samples,&pos,target,cancel,context) || !midi_event(font,events[i])) goto fail;
    }
    for(i=0;i<16;i++) tsf_channel_note_off_all(font,(int)i);
    if(!render_until(font,samples,&pos,total,cancel,context)) goto fail;
    free(events); tsf_close(font); *pcm=samples; *frames=total; return 1;
fail: free(events); free(samples); tsf_close(font); return 0;
}
