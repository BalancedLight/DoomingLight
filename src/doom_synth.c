#include "doom_synth.h"
#include "doom_music.h"
#include "../third_party/opl/opl3.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct Voice { int active, held, channel, note, pitch; uint8_t velocity, fine, feedback, scale, level, mod_scale, mod_level; } Voice;
typedef struct Channel { uint8_t program, volume, expression, pan, sustain; int bend; } Channel;
static const unsigned operators[9]={0,1,2,8,9,10,16,17,18};
static unsigned reg_voice(unsigned i) { return (i%9)|((i/9)<<8); }
static unsigned reg_op(unsigned i) { return operators[i%9]|((i/9)<<8); }
static void op_write(opl3_chip *chip,unsigned op,const uint8_t *p,unsigned level) {
    OPL3_WriteReg(chip,(uint16_t)(0x20+op),p[0]);
    OPL3_WriteReg(chip,(uint16_t)(0x40+op),(uint8_t)((p[4]&0xc0)|(level&63)));
    OPL3_WriteReg(chip,(uint16_t)(0x60+op),p[1]);
    OPL3_WriteReg(chip,(uint16_t)(0x80+op),p[2]);
    OPL3_WriteReg(chip,(uint16_t)(0xe0+op),p[3]);
}
static void keyoff(opl3_chip *chip,Voice *v,unsigned index) {
    OPL3_WriteReg(chip,(uint16_t)(0xb0+reg_voice(index)),0); v->active=0;
}
static void frequency(opl3_chip *chip,Voice *v,Channel *c,unsigned index) {
    double note=v->pitch+(c->bend-8192)/4096.0+(v->fine-128)/256.0;
    double hz=440.0*pow(2.0,(note-69.0)/12.0); unsigned block=0,fnum;
    while(block<7 && hz*1048576.0/(49716.0*(1u<<block))>1023.0) block++;
    fnum=(unsigned)(hz*1048576.0/(49716.0*(1u<<block))); if(fnum>1023) fnum=1023;
    OPL3_WriteReg(chip,(uint16_t)(0xa0+reg_voice(index)),(uint8_t)fnum);
    OPL3_WriteReg(chip,(uint16_t)(0xb0+reg_voice(index)),(uint8_t)(0x20|(block<<2)|(fnum>>8)));
}
static void volume(opl3_chip *chip,Voice *v,Channel *c,unsigned index) {
    unsigned amplitude=(unsigned)v->velocity*c->volume*c->expression/(127*127);
    unsigned attenuation=v->level+(63-v->level)*(127-amplitude)/127, op=reg_op(index);
    OPL3_WriteReg(chip,(uint16_t)(0x40+op+3),(uint8_t)((v->scale&0xc0)|attenuation));
    if(v->feedback&1) {
        unsigned mod=v->mod_level>attenuation?v->mod_level:attenuation;
        OPL3_WriteReg(chip,(uint16_t)(0x40+op),(uint8_t)((v->mod_scale&0xc0)|mod));
    }
}
static void pan(opl3_chip *chip,Voice *v,Channel *c,unsigned index) {
    unsigned flags=c->pan<32?0x10:c->pan>95?0x20:0x30;
    OPL3_WriteReg(chip,(uint16_t)(0xc0+reg_voice(index)),(uint8_t)(v->feedback|flags));
}
static void handle(opl3_chip *chip,Voice *voices,Channel *channels,const uint8_t *bank,DoomMusicEvent e) {
    unsigned ch=e.status&15,type=e.status&0xf0,i; Channel *c=&channels[ch];
    if(type==0xc0) { c->program=e.data1; return; }
    if(type==0xe0) { c->bend=e.data1|((int)e.data2<<7); for(i=0;i<18;i++) if(voices[i].active&&voices[i].channel==(int)ch) frequency(chip,&voices[i],c,i); return; }
    if(type==0xb0) {
        if(e.data1==7) c->volume=e.data2;
        else if(e.data1==11) c->expression=e.data2;
        else if(e.data1==10) c->pan=e.data2;
        else if(e.data1==64) { c->sustain=e.data2>=64; if(!c->sustain) for(i=0;i<18;i++) if(voices[i].active&&voices[i].channel==(int)ch&&!voices[i].held) keyoff(chip,&voices[i],i); }
        else if(e.data1==120||e.data1==123) { for(i=0;i<18;i++) if(voices[i].active&&voices[i].channel==(int)ch) keyoff(chip,&voices[i],i); }
        else if(e.data1==121) { c->volume=100;c->expression=127;c->pan=64;c->bend=8192;c->sustain=0; }
        for(i=0;i<18;i++) if(voices[i].active&&voices[i].channel==(int)ch) { volume(chip,&voices[i],c,i); pan(chip,&voices[i],c,i); }
        return;
    }
    if(type==0x80||(type==0x90&&!e.data2)) {
        for(i=0;i<18;i++) if(voices[i].active&&voices[i].channel==(int)ch&&voices[i].note==e.data1) { voices[i].held=0; if(!c->sustain) keyoff(chip,&voices[i],i); }
    } else if(type==0x90) {
        const uint8_t *inst; unsigned part,parts;
        if(ch==9) { if(e.data1<35||e.data1>81) return; inst=bank+8+(128+e.data1-35)*36; }
        else inst=bank+8+c->program*36;
        parts=(inst[0]&4)?2:1;
        for(part=0;part<parts;part++) {
            const uint8_t *p=inst+4+part*16; unsigned op; int pitch;
            for(i=0;i<18&&voices[i].active;i++) {} if(i==18) i=part;
            if(voices[i].active) keyoff(chip,&voices[i],i);
            op=reg_op(i); op_write(chip,op,p,p[5]); op_write(chip,op+3,p+7,63);
            pitch=(inst[0]&1)?inst[3]:(int)e.data1+(int16_t)(p[14]|(p[15]<<8));
            while(pitch<0) pitch+=12; while(pitch>95) pitch-=12;
            voices[i].active=voices[i].held=1; voices[i].channel=(int)ch; voices[i].note=e.data1;
            voices[i].pitch=pitch; voices[i].fine=part?inst[2]:128; voices[i].velocity=e.data2;
            voices[i].feedback=p[6]; voices[i].scale=p[11]; voices[i].level=p[12]&63; voices[i].mod_scale=p[4]; voices[i].mod_level=p[5]&63;
            volume(chip,&voices[i],c,i); pan(chip,&voices[i],c,i); frequency(chip,&voices[i],c,i);
        }
    }
}
static int generate(opl3_chip *chip,int16_t *samples,size_t *frame,size_t target,unsigned rate,DoomSynthCancelFn canceled,void *context) {
    while(*frame<target) {
        size_t n=target-*frame; if(n>rate/10) n=rate/10;
        if(canceled&&canceled(context)) return 0;
        OPL3_GenerateStream(chip,samples+*frame*2,(uint32_t)n); *frame+=n;
    }
    return 1;
}
int doom_synth_render_cancelable(const uint8_t *music,size_t size,const uint8_t *bank,size_t bank_size,int16_t **pcm,size_t *frames,unsigned rate,DoomSynthCancelFn canceled,void *context) {
    size_t count,i,frame=0,total; DoomMusicEvent *events; int16_t *samples;
    opl3_chip chip; Voice voices[18]={{0}}; Channel channels[16]; int midi;
    if(!pcm||!frames) return 0; *pcm=NULL; *frames=0;
    if(!music||size<4||!bank||bank_size<8+175*36||memcmp(bank,"#OPL_II#",8)||rate<8000||rate>48000) return 0;
    if(canceled&&canceled(context)) return 0;
    midi=!memcmp(music,"MThd",4);
    count=midi?doom_music_parse_midi(music,size,NULL,0):doom_music_parse_mus(music,size,NULL,0);
    if(!count||count>1000000) return 0;
    if(canceled&&canceled(context)) return 0;
    events=(DoomMusicEvent*)malloc(count*sizeof(*events)); if(!events) return 0;
    if((midi?doom_music_parse_midi(music,size,events,count):doom_music_parse_mus(music,size,events,count))!=count||events[count-1].tick>140*600) {free(events);return 0;}
    total=(size_t)events[count-1].tick*rate/140+rate*2;
    samples=(int16_t*)malloc(total*2*sizeof(*samples)); if(!samples){free(events);return 0;}
    memset(channels,0,sizeof channels); for(i=0;i<16;i++){channels[i].volume=100;channels[i].expression=127;channels[i].pan=64;channels[i].bend=8192;}
    OPL3_Reset(&chip,rate); OPL3_WriteReg(&chip,0x105,1); OPL3_WriteReg(&chip,1,0x20);
    for(i=0;i<count;i++) {size_t target=(size_t)events[i].tick*rate/140; if((canceled&&canceled(context))||!generate(&chip,samples,&frame,target,rate,canceled,context)) {free(events);free(samples);return 0;} handle(&chip,voices,channels,bank,events[i]);}
    for(i=0;i<18;i++) if(voices[i].active) keyoff(&chip,&voices[i],(unsigned)i);
    if(!generate(&chip,samples,&frame,total,rate,canceled,context)) {free(events);free(samples);return 0;}
    free(events);*pcm=samples;*frames=total;return 1;
}
int doom_synth_render(const uint8_t *music,size_t size,const uint8_t *bank,size_t bank_size,int16_t **pcm,size_t *frames,unsigned rate) {
    return doom_synth_render_cancelable(music,size,bank,bank_size,pcm,frames,rate,NULL,NULL);
}
int doom_synth_render_mus(const uint8_t *music,size_t size,const uint8_t *bank,size_t bank_size,int16_t **pcm,size_t *frames,unsigned rate) {
    if(!music||size<4||memcmp(music,"MUS\x1a",4)) return 0;
    return doom_synth_render(music,size,bank,bank_size,pcm,frames,rate);
}
