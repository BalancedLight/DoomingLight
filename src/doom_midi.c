#include "doom_music.h"
#include <stdlib.h>
#include <string.h>

typedef struct MidiEntry { uint32_t tick, ordinal, tempo; DoomMusicEvent event; } MidiEntry;
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0]<<8)|p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static int variable(const uint8_t *data,size_t end,size_t *pos,uint32_t *value) {
    unsigned i; uint32_t n=0; uint8_t c;
    for(i=0;i<4;i++) { if(*pos>=end) return 0; c=data[(*pos)++]; n=(n<<7)|(c&127); if(!(c&128)){*value=n; return 1;} }
    return 0;
}
static int compare(const void *a,const void *b) {
    const MidiEntry *x=(const MidiEntry*)a,*y=(const MidiEntry*)b;
    if(x->tick!=y->tick) return x->tick<y->tick?-1:1;
    return x->ordinal<y->ordinal?-1:x->ordinal>y->ordinal?1:0;
}
size_t doom_music_parse_midi(const uint8_t *data,size_t size,DoomMusicEvent *out,size_t capacity) {
    MidiEntry *items=NULL; size_t count=0,allocated=0,pos=0,track,output=0,i;
    uint32_t tracks,division,last=0,tempo=500000; uint64_t micros=0;
    if(!data || size<14 || memcmp(data,"MThd",4) || be32(data+4)<6 || be32(data+4)>size-8) return 0;
    tracks=be16(data+10); division=be16(data+12);
    if(be16(data+8)>1 || !tracks || tracks>128 || !division || division&0x8000) return 0;
    pos=8+be32(data+4);
    for(track=0;track<tracks;track++) {
        size_t end; uint32_t tick=0,delta; uint8_t running=0; int ended=0;
        if(pos>size || size-pos<8 || memcmp(data+pos,"MTrk",4)) goto fail;
        if(be32(data+pos+4)>size-pos-8) goto fail;
        end=pos+8+be32(data+pos+4); pos+=8;
        while(pos<end) {
            uint8_t status,a=0,b=0; uint32_t len; MidiEntry entry;
            if(!variable(data,end,&pos,&delta) || tick>UINT32_MAX-delta || pos>=end) goto fail;
            tick+=delta;
            status=data[pos];
            if(status&128) { pos++; if(status<0xf0) running=status; }
            else { if(!running) goto fail; status=running; }
            memset(&entry,0,sizeof entry); entry.tick=tick; entry.ordinal=(uint32_t)count;
            if(status==0xff) {
                uint8_t meta; if(pos>=end) goto fail; meta=data[pos++];
                if(!variable(data,end,&pos,&len) || len>end-pos) goto fail;
                if(meta==0x2f) { if(len) goto fail; ended=1; pos=end; continue; }
                if(meta==0x51) { if(len!=3) goto fail; entry.tempo=((uint32_t)data[pos]<<16)|(data[pos+1]<<8)|data[pos+2]; if(!entry.tempo) goto fail; }
                pos+=len; if(meta!=0x51) continue;
            } else if(status==0xf0 || status==0xf7) {
                if(!variable(data,end,&pos,&len) || len>end-pos) goto fail; pos+=len; running=0; continue;
            } else {
                unsigned kind=status&0xf0; if(status>=0xf0 || pos>=end) goto fail;
                a=data[pos++]; if(a&128) goto fail;
                if(kind!=0xc0 && kind!=0xd0) { if(pos>=end || data[pos]&128) goto fail; b=data[pos++]; }
                entry.event.status=status; entry.event.data1=a; entry.event.data2=b;
            }
            if(count>=1000000) goto fail;
            if(count==allocated) { size_t next=allocated?allocated*2:256; MidiEntry *p;
                if(next>1000000) next=1000000; p=(MidiEntry*)realloc(items,next*sizeof(*items));
                if(!p) goto fail; items=p; allocated=next;
            }
            items[count++]=entry;
        }
        if(!ended) goto fail;
    }
    qsort(items,count,sizeof(*items),compare);
    for(i=0;i<count;i++) {
        uint64_t duration=(uint64_t)(items[i].tick-last)*tempo;
        if(duration>UINT64_MAX-micros) goto fail;
        micros+=duration; last=items[i].tick;
        if(micros/(uint64_t)division>600000000ULL) goto fail;
        if(items[i].tempo) { tempo=items[i].tempo; continue; }
        if(out) {
            if(output>=capacity) goto fail;
            out[output]=items[i].event;
            out[output].tick=(uint32_t)((micros*140ULL)/(division*1000000ULL));
        }
        output++;
    }
    free(items); return output;
fail: free(items); return 0;
}
