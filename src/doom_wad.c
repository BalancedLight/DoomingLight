#define _CRT_SECURE_NO_WARNINGS
#include "doom_wad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#endif

struct DoomWad { uint8_t *bytes; size_t size; uint32_t count, directory; int doom2; };
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static int range(size_t size, size_t off, size_t len) { return off <= size && len <= size-off; }
static FILE *open_utf8(const char *path) {
#ifdef _WIN32
    wchar_t *wide; int n; FILE *f;
    n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (!n) return NULL;
    wide = (wchar_t *)malloc((size_t)n * sizeof(*wide));
    if (!wide) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n)) { free(wide); return NULL; }
    f = _wfopen(wide, L"rb"); free(wide); return f;
#else
    return fopen(path, "rb");
#endif
}
int doom_wad_lump(const DoomWad *w, const char *name, DoomLump *out) {
    char key[8] = {0}; size_t n, j; uint32_t i;
    if (!w || !name || !out) return 0;
    n = strlen(name); if (!n || n > 8) return 0;
    for (j=0;j<n;j++) key[j]=(char)toupper((unsigned char)name[j]);
    for (i=w->count;i>0;i--) {
        const uint8_t *entry=w->bytes+w->directory+(size_t)(i-1)*16;
        uint32_t off=u32(entry), len=u32(entry+4);
        if (!memcmp(entry+8,key,8)) { out->data=w->bytes+off; out->size=len; return 1; }
    }
    out->data=NULL; out->size=0; return 0;
}
DoomWad *doom_wad_open(const char *path) {
    FILE *f; long length; DoomWad *w; uint32_t i; DoomLump lump;
    if (!path || !(f=open_utf8(path))) return NULL;
    if (fseek(f,0,SEEK_END) || (length=ftell(f))<12 || length>268435456L || fseek(f,0,SEEK_SET)) { fclose(f); return NULL; }
    w=(DoomWad *)calloc(1,sizeof(*w)); if (!w) { fclose(f); return NULL; }
    w->size=(size_t)length; w->bytes=(uint8_t *)malloc(w->size);
    if (!w->bytes || fread(w->bytes,1,w->size,f)!=w->size) { fclose(f); doom_wad_close(w); return NULL; }
    fclose(f);
    if (memcmp(w->bytes,"IWAD",4)) { doom_wad_close(w); return NULL; }
    w->count=u32(w->bytes+4); w->directory=u32(w->bytes+8);
    if (w->count>1000000 || !range(w->size,w->directory,(size_t)w->count*16)) { doom_wad_close(w); return NULL; }
    for (i=0;i<w->count;i++) { const uint8_t *e=w->bytes+w->directory+(size_t)i*16; if (!range(w->size,u32(e),u32(e+4))) { doom_wad_close(w); return NULL; } }
    if (!doom_wad_lump(w,"PLAYPAL",&lump) || lump.size<768 || !doom_wad_lump(w,"STBAR",&lump)) { doom_wad_close(w); return NULL; }
    w->doom2=doom_wad_lump(w,"MAP01",&lump);
    if ((!w->doom2 && (!doom_wad_lump(w,"E1M1",&lump) || !doom_wad_lump(w,"E2M1",&lump) || !doom_wad_lump(w,"E3M1",&lump))) ||
        !doom_wad_lump(w,"PISGA0",&lump) || !doom_wad_lump(w,"SHTGA0",&lump) ||
        !doom_wad_lump(w,"CHGGA0",&lump) || !doom_wad_lump(w,"MISGA0",&lump) ||
        !doom_wad_lump(w,"PLSGA0",&lump) || !doom_wad_lump(w,"BFGGA0",&lump) ||
        !doom_wad_lump(w,"SAWGA0",&lump) || (w->doom2 && !doom_wad_lump(w,"SHT2A0",&lump))) {
        doom_wad_close(w); return NULL;
    }
    return w;
}
void doom_wad_close(DoomWad *w) { if (w) { free(w->bytes); free(w); } }
int doom_wad_is_doom2(const DoomWad *w) { return w && w->doom2; }
int doom_wad_patch(const DoomWad *w, const char *name, DoomImage *out) {
    DoomLump p,pal; uint32_t width,height,x; uint8_t *rgba;
    if (!out) return 0; memset(out,0,sizeof(*out));
    if (!doom_wad_lump(w,name,&p) || p.size<8 || !doom_wad_lump(w,"PLAYPAL",&pal) || pal.size<768) return 0;
    width=u16(p.data); height=u16(p.data+2);
    if (!width || !height || width>4096 || height>4096 || !range(p.size,8,(size_t)width*4)) return 0;
    rgba=(uint8_t *)calloc((size_t)width*height,4); if (!rgba) return 0;
    for (x=0;x<width;x++) {
        size_t off=u32(p.data+8+x*4); int previous=-1; int terminated=0;
        while (off<p.size) {
            uint32_t y, count, k; int top=p.data[off++];
            if (top==255) { terminated=1; break; }
            if (!range(p.size,off,2)) break;
            count=p.data[off]; off+=2;
            if (!range(p.size,off,(size_t)count+1)) break;
            if (top<=previous) top+=previous; previous=top;
            for(k=0;k<count;k++) { y=(uint32_t)top+k; if(y<height) { uint8_t index=p.data[off+k]; uint8_t *dst=rgba+((size_t)y*width+x)*4; dst[0]=pal.data[index*3]; dst[1]=pal.data[index*3+1]; dst[2]=pal.data[index*3+2]; dst[3]=255; } }
            off+=(size_t)count+1;
        }
        if (!terminated) { free(rgba); return 0; }
    }
    out->width=width; out->height=height; out->left=(int16_t)u16(p.data+4); out->top=(int16_t)u16(p.data+6); out->rgba=rgba; return 1;
}
void doom_image_free(DoomImage *image) { if(image) { free(image->rgba); memset(image,0,sizeof(*image)); } }
int doom_wad_sound(const DoomWad *w, const char *name, DoomSound *out) {
    DoomLump lump; uint32_t count,i; int16_t *samples;
    if (!out) return 0; memset(out,0,sizeof(*out));
    if (!doom_wad_lump(w,name,&lump) || lump.size<8 || u16(lump.data)!=3) return 0;
    count=u32(lump.data+4); if (!u16(lump.data+2) || count>16777216 || !range(lump.size,8,count)) return 0;
    samples=(int16_t *)malloc((size_t)count*sizeof(*samples)); if(!samples) return 0;
    for(i=0;i<count;i++) samples[i]=(int16_t)(((int)lump.data[8+i]-128)*256);
    out->rate=u16(lump.data+2); out->count=count; out->samples=samples; return 1;
}
void doom_sound_free(DoomSound *sound) { if(sound) { free(sound->samples); memset(sound,0,sizeof(*sound)); } }
