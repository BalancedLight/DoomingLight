#include "doom_world_renderer.h"
#include "doom_world_lights.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

/* Uses engine-owned materials and resources without hooking the renderer. */
#ifdef DOOM_MODE_RUNTIME
namespace {
struct EngineString { const char *data; unsigned size, capacity; };
using CreateDef = void *(__fastcall *)(void **);
using ParseParticle = unsigned char (__fastcall *)(void *, const char *, const char *);
using ParseEmitter = unsigned char (__fastcall *)(void *, void *, const char *, const char *);
using GetFx = void *(__fastcall *)(void *);
using CreateFx = unsigned (__fastcall *)(void *, void *, void *, const float *, const float *, void *, int);
using MoveFx = unsigned char (__fastcall *)(void *, unsigned, const float *, const float *, const float *);
using KillFx = void (__fastcall *)(void *, unsigned);
using ReleaseDef = void (__fastcall *)(void *, void *, void *);
using CreateTexture = void *(__fastcall *)(void *, const EngineString *, int, int, unsigned, unsigned char, unsigned);
using UpdateTexture = void (__fastcall *)(void *, void *, const void *, int, unsigned, int, unsigned, unsigned, unsigned, unsigned, unsigned);
using ReleaseTexture = unsigned char (__fastcall *)(void *, void *);
using CloneMaterial = void *(__fastcall *)(void *, void *, const char *);
using ReleaseMaterial = void (__fastcall *)(void *, void **);
using SetTexture = unsigned char (__fastcall *)(void *, unsigned char, void *);
using GetSampler = void *(__fastcall *)(void *, uint32_t);
struct Slot { void *emitter, *particle, *texture; unsigned char *pixels; unsigned handle, serial, edge, light; };
struct Impl { unsigned char *base; void *level, *fx, *defs, *mats; Slot slots[64]; DoomWorldLights *lights; unsigned generation; bool quarantined, bilinear, light_attempted; };
volatile LONG world_serial = 0;
bool finite_position(const float *p) { return p && std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]); }
void trace_failure(const char *message) {
#ifdef DOOM_MODE_LAB
    OutputDebugStringA(message);
#else
    (void)message;
#endif
}
bool identities(Impl *r,void *level,uint32_t generation) {
    if(!r || r->quarantined || !level || level!=r->level || generation!=r->generation) return false;
    __try {
        HMODULE loaded=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
             reinterpret_cast<LPCWSTR>(r->base),&loaded) || loaded!=reinterpret_cast<HMODULE>(r->base)) return false;
        return reinterpret_cast<GetFx>(r->base+0x2dded0)(level)==r->fx &&
            *reinterpret_cast<void **>(r->base+0xaccc40)==r->defs &&
            *reinterpret_cast<void **>(r->base+0xacd118)==r->mats &&
            *reinterpret_cast<void **>(r->fx)==r->base+0x8e26f8 &&
            *reinterpret_cast<void **>(r->mats)==r->base+0x94ac48;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void release_def(Impl *r, void *&def, unsigned registry_offset) {
    if (def && r->defs) { reinterpret_cast<ReleaseDef>(r->base+0x204660)(r->defs,def,static_cast<unsigned char *>(r->defs)+registry_offset); def=nullptr; }
}
void clear_slot(Impl *r, Slot &s) {
    if (s.light && r->lights) {
        doom_world_lights_remove(r->lights,r->level,r->generation,s.light);
        s.light=0;
    }
    if (s.handle && r->fx) {
        auto **vtable=*reinterpret_cast<void ***>(r->fx);
        using StopFx = unsigned char (__fastcall *)(void *, unsigned);
        reinterpret_cast<StopFx>(vtable[2])(r->fx,s.handle);
        reinterpret_cast<KillFx>(vtable[35])(r->fx,s.handle);
        s.handle=0;
    }
    release_def(r,s.particle,72); release_def(r,s.emitter,48);
    if (s.texture) { reinterpret_cast<ReleaseTexture>(r->base+0x29a9f0)(nullptr,s.texture); s.texture=nullptr; }
    if (s.pixels) { HeapFree(GetProcessHeap(),0,s.pixels); s.pixels=nullptr; }
    s.edge=0;
    s.serial=0;
}
/* The material pack owns sampler-cache records. A cloned material has its own
 * texture/sampler pointer pairs and borrows those records for the pack's life. */
bool set_point_sampler(Impl *r, void *clone, void *source, void *texture) {
    auto *material=*reinterpret_cast<unsigned char **>(static_cast<unsigned char *>(clone)+8);
    auto *source_material=*reinterpret_cast<unsigned char **>(static_cast<unsigned char *>(source)+8);
    if (!material || !source_material || material == source_material) return false;
    auto *record=*reinterpret_cast<unsigned char **>(material);
    void *pack=*reinterpret_cast<void **>(material+24);
    if (!record || !pack || pack!=*reinterpret_cast<void **>(source_material+24)) return false;
    const unsigned techniques=material[48], textures=material[49];
    if (techniques>4 || !textures || textures>32 ||
        *reinterpret_cast<const uint16_t *>(record+16)!=techniques ||
        *reinterpret_cast<const uint16_t *>(record+18)!=textures) return false;
    const unsigned offset=*reinterpret_cast<const uint16_t *>(record+22);
    if (offset<2) return false;
    const uint32_t sampler_word=*reinterpret_cast<const uint32_t *>(record+22+offset);
    const unsigned filter=(sampler_word>>16)&15u;
    if (filter>8) return false;
    auto **pair=reinterpret_cast<void **>(material+64+32*techniques);
    if (pair[0]!=texture || !pair[1]) return false;
    auto get_sampler=reinterpret_cast<GetSampler>(r->base+0x82d050);
    if (get_sampler(pack,sampler_word)!=pair[1] ||
        *reinterpret_cast<const uint32_t *>(static_cast<unsigned char *>(pair[1])+8)!=sampler_word) return false;
    const uint32_t point_word=sampler_word & ~0x000f0000u;
    void *point=get_sampler(pack,point_word);
    if (!point || !*reinterpret_cast<void **>(point) ||
        *reinterpret_cast<const uint32_t *>(static_cast<unsigned char *>(point)+8)!=point_word) return false;
    pair[1]=point;
    return true;
}
}
struct DoomWorldRenderer { Impl impl; };
extern "C" DoomWorldRenderer *doom_world_renderer_create(void *module, void *level,uint32_t generation,int bilinear) {
    if (!module || !level) return nullptr;
    auto *r=static_cast<DoomWorldRenderer *>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(DoomWorldRenderer)));
    if (!r) return nullptr;
    __try {
        r->impl.base=static_cast<unsigned char *>(module);
        r->impl.level=level; r->impl.generation=generation; r->impl.bilinear=bilinear!=0;
        auto *b=r->impl.base;
        static const struct { unsigned rva; unsigned char bytes[12]; } checks[]={
          {0x2dded0,{0x48,0x8b,0x41,0x08,0x48,0x05,0xb8,0,0,0,0xc3,0xcc}},
          {0x1fd750,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xb9,0x10,0x01}},
          {0x1fd790,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xb9,0xa0,0x02}},
          {0x21faa0,{0x40,0x57,0x41,0x54,0x41,0x55,0x48,0x81,0xec,0xd0,0,0}},
          {0x21d830,{0x40,0x55,0x57,0x41,0x54,0x41,0x55,0x48,0x81,0xec,0xa8,0}},
          {0x1e72e0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89}},
          {0x1e72a0,{0x48,0x83,0xec,0x28,0x48,0x8b,0x41,0x08,0x48,0x8b,0x88,0xd0}},
          {0x1e23e0,{0x48,0x83,0xec,0x28,0x48,0x8b,0x41,0x08,0x48,0x8b,0x88,0xd0}},
          {0x1ec770,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,0x18,0x56}},
          {0x204660,{0x48,0x85,0xd2,0x0f,0x84,0xfb,0,0,0,0x48,0x89,0x6c}},
          {0x29c1c0,{0x48,0x83,0xec,0x68,0x48,0x8b,0x02,0x48,0x8d,0x15,0x73,0xb1}},
          {0x29c230,{0x48,0x83,0xec,0x68,0x8b,0x84,0x24,0xc0,0,0,0,0x48}},
          {0x29a9f0,{0x48,0x8d,0x0d,0xa9,0x46,0x8b,0,0xe9,0x84,0x26,0x51,0}},
          {0x8040c0,{0x48,0x89,0x54,0x24,0x10,0x53,0x57,0x41,0x55,0x41,0x56,0x41}},
          {0x82d050,{0x89,0x54,0x24,0x10,0x53,0x48,0x83,0xec,0x60,0x45,0x33,0xc0}},
        };
        for (const auto &check:checks) if (std::memcmp(b+check.rva,check.bytes,sizeof check.bytes)) __leave;
        r->impl.fx=reinterpret_cast<GetFx>(b+0x2dded0)(level);
        r->impl.defs=*reinterpret_cast<void **>(b+0xaccc40);
        r->impl.mats=*reinterpret_cast<void **>(b+0xacd118);
        if (!r->impl.fx || !r->impl.defs || !r->impl.mats ||
            *reinterpret_cast<void **>(r->impl.fx)!=b+0x8e26f8 ||
            *reinterpret_cast<void **>(r->impl.mats)!=b+0x94ac48) {
            trace_failure("Doom world: owner identity unavailable\n"); __leave;
        }
        return r;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    HeapFree(GetProcessHeap(),0,r); return nullptr;
}
extern "C" uint32_t doom_world_renderer_add(DoomWorldRenderer *owner,void *level,uint32_t generation,const uint8_t *rgba,unsigned edge,const float position[3],float size,unsigned flags) {
    if (!owner || !rgba || !finite_position(position) || edge<16 || edge>512 || (edge&(edge-1)) || !(size>0 && size<=8)) return 0;
    Impl *r=&owner->impl; Slot *s=nullptr; char script[512], unique[64];
    if(!identities(r,level,generation)) return 0;
    for (auto &entry:r->slots) if(!entry.serial) { s=&entry; break; }
    if (!s) return 0;
    s->serial=static_cast<unsigned>(InterlockedIncrement(&world_serial));
    if(!s->serial) s->serial=static_cast<unsigned>(InterlockedIncrement(&world_serial));
    __try {
        auto *b=r->base; void *original=nullptr, *clone=nullptr;
        s->edge=edge;
        s->pixels=static_cast<unsigned char *>(HeapAlloc(GetProcessHeap(),0,static_cast<SIZE_T>(edge)*edge*4u));
        if(!s->pixels) __leave;
        std::memcpy(s->pixels,rgba,static_cast<SIZE_T>(edge)*edge*4u);
        char texture_name[64]; std::snprintf(texture_name,sizeof texture_name,"doom_world_%08x",s->serial);
        std::snprintf(unique,sizeof unique,"doom_world_material_%08x",s->serial);
        EngineString name{texture_name,static_cast<unsigned>(std::strlen(texture_name)),static_cast<unsigned>(std::strlen(texture_name))};
        s->texture=reinterpret_cast<CreateTexture>(b+0x29c1c0)(nullptr,&name,static_cast<int>(edge),static_cast<int>(edge),1,0,2);
        if(!s->texture) { trace_failure("Doom world: texture creation failed\n"); __leave; }
        reinterpret_cast<UpdateTexture>(b+0x29c230)(nullptr,s->texture,s->pixels,0,0,0,0,edge,edge,edge*4,32);
        reinterpret_cast<CreateDef>(b+0x1fd750)(&s->emitter);
        reinterpret_cast<CreateDef>(b+0x1fd790)(&s->particle);
        if(!s->emitter||!s->particle) { trace_failure("Doom world: definition creation failed\n"); __leave; }
        const char *emitter="EmiterDef(){ Meter() Space(\"local\") GenType(\"one_time\") GenArea(\"point\") LifeTime(){keyf(0,0)} NumParticles(){keyf(0,1)} }";
        if(!reinterpret_cast<ParseEmitter>(b+0x21d830)(nullptr,s->emitter,"doom_emitter.fx",emitter)) { trace_failure("Doom world: emitter definition rejected\n"); __leave; }
        const char *material=(flags & DOOM_WORLD_SPRITE_PICKUP) ? "dw_soft_smoke.mat" : "dw_soft_smoke_vfb.mat";
        std::snprintf(script,sizeof script,"ParticleDef(){ Meter() LifeTime(){keyf(0,600)} GravityMul(){keyf(0,0)} Drag(){keyf(0,0)} WindInfluence(){keyf(0,0)} Size(){keyf(0,%f)} Color(){keyv4(0,1,1,1,1)} Material(\"%s\",\"%s\") }",size,material,material);
        if(!reinterpret_cast<ParseParticle>(b+0x21faa0)(s->particle,"doom_particle.fx",script)) { trace_failure("Doom world: particle definition rejected\n"); __leave; }
        auto **material_ptr=reinterpret_cast<void **>(static_cast<unsigned char *>(s->particle)+104);
        original=*material_ptr; if(!original) { trace_failure("Doom world: source material unavailable\n"); __leave; }
        auto **mv=*reinterpret_cast<void ***>(r->mats);
        if(mv[18]!=b+0x806b60 || mv[4]!=b+0x8058e0) __leave;
        clone=reinterpret_cast<CloneMaterial>(mv[18])(r->mats,original,unique);
        if(!clone) { trace_failure("Doom world: material clone failed\n"); __leave; }
        auto **cv=*reinterpret_cast<void ***>(clone);
        if(cv[10]!=b+0x816bd0 || !reinterpret_cast<SetTexture>(cv[10])(clone,0,s->texture)) {
            reinterpret_cast<ReleaseMaterial>(mv[4])(r->mats,&clone); __leave;
        }
        if(!r->bilinear && !set_point_sampler(r,clone,original,s->texture)) {
            trace_failure("Doom world: point sampler unavailable\n");
            reinterpret_cast<ReleaseMaterial>(mv[4])(r->mats,&clone); __leave;
        }
        reinterpret_cast<ReleaseMaterial>(mv[4])(r->mats,material_ptr);
        *material_ptr=clone;
        auto **fxv=*reinterpret_cast<void ***>(r->fx);
        if(fxv[37]!=b+0x1e72e0 || fxv[70]!=b+0x1ec770 || fxv[35]!=b+0x1e72a0 || fxv[2]!=b+0x1e23e0) __leave;
        const float up[3]={0,1,0};
        s->handle=reinterpret_cast<CreateFx>(fxv[37])(r->fx,s->emitter,s->particle,position,up,nullptr,-1);
        if(!s->handle) { trace_failure("Doom world: emitter creation failed\n"); __leave; }
        if (flags & (DOOM_WORLD_SPRITE_LIGHT_PLASMA | DOOM_WORLD_SPRITE_LIGHT_BFG |
                     DOOM_WORLD_SPRITE_LIGHT_ROCKET_FLASH | DOOM_WORLD_SPRITE_LIGHT_PLASMA_FLASH |
                     DOOM_WORLD_SPRITE_LIGHT_BFG_FLASH | DOOM_WORLD_SPRITE_LIGHT_BFG_TRACER)) {
            if (!r->light_attempted) {
                r->light_attempted=true;
                r->lights=doom_world_lights_create(r->base,r->level,r->generation);
            }
            const int kind = (flags & DOOM_WORLD_SPRITE_LIGHT_BFG_TRACER) ? DOOM_WORLD_LIGHT_BFG_TRACER :
                             (flags & DOOM_WORLD_SPRITE_LIGHT_BFG_FLASH) ? DOOM_WORLD_LIGHT_BFG_FLASH :
                             (flags & DOOM_WORLD_SPRITE_LIGHT_PLASMA_FLASH) ? DOOM_WORLD_LIGHT_PLASMA_FLASH :
                             (flags & DOOM_WORLD_SPRITE_LIGHT_ROCKET_FLASH) ? DOOM_WORLD_LIGHT_ROCKET_FLASH :
                             (flags & DOOM_WORLD_SPRITE_LIGHT_BFG) ? DOOM_WORLD_LIGHT_BFG : DOOM_WORLD_LIGHT_PLASMA;
            if (r->lights) s->light=doom_world_lights_add(r->lights,r->level,r->generation,position,kind);
        }
        trace_failure("Doom world: sprite created\n");
        return s->serial;
    } __except(EXCEPTION_EXECUTE_HANDLER) { r->quarantined=true; return 0; }
    __try { clear_slot(r,*s); } __except(EXCEPTION_EXECUTE_HANDLER) { r->quarantined=true; }
    return 0;
}
extern "C" int doom_world_renderer_update(DoomWorldRenderer *owner,void *level,uint32_t generation,uint32_t token,const uint8_t *rgba,unsigned edge) {
    if(!owner || !token || !rgba || !identities(&owner->impl,level,generation)) return 0;
    __try {
        for(auto &s:owner->impl.slots) if(s.serial==token && s.texture && s.pixels && s.edge==edge) {
            std::memcpy(s.pixels,rgba,static_cast<SIZE_T>(edge)*edge*4u);
            reinterpret_cast<UpdateTexture>(owner->impl.base+0x29c230)(nullptr,s.texture,s.pixels,0,0,0,0,edge,edge,edge*4,32);
            return 1;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { owner->impl.quarantined=true; }
    return 0;
}
extern "C" int doom_world_renderer_move(DoomWorldRenderer *owner,void *level,uint32_t generation,uint32_t token,const float position[3]) {
    if(!owner || !token || !finite_position(position) || !identities(&owner->impl,level,generation)) return 0;
    __try {
        for(auto &s:owner->impl.slots) if(s.serial==token && s.handle) {
            auto **v=*reinterpret_cast<void ***>(owner->impl.fx);
            if(v[70]!=owner->impl.base+0x1ec770) return 0;
            /* FXSetPosition edits a local curve which is multiplied by the
             * spawn transform. Update its world-space start transform instead. */
            const int moved=reinterpret_cast<MoveFx>(v[70])(owner->impl.fx,s.handle,position,nullptr,nullptr)!=0;
            if (moved && s.light && owner->impl.lights)
                doom_world_lights_move(owner->impl.lights,level,generation,s.light,position);
            return moved;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { owner->impl.quarantined=true; } return 0;
}
extern "C" int doom_world_renderer_remove(DoomWorldRenderer *owner,void *level,uint32_t generation,uint32_t token) {
    if(!owner || !token || !identities(&owner->impl,level,generation)) return 0;
    __try { for(auto &s:owner->impl.slots) if(s.serial==token) { clear_slot(&owner->impl,s); return 1; } } __except(EXCEPTION_EXECUTE_HANDLER) { owner->impl.quarantined=true; } return 0;
}
extern "C" int doom_world_renderer_destroy(DoomWorldRenderer *owner,void *level,uint32_t generation) {
    if(!owner) return 1;
    if(!owner->impl.quarantined) {
        bool empty=true;
        for(const auto &s:owner->impl.slots) if(s.serial) { empty=false; break; }
        if(empty && !owner->impl.lights) { HeapFree(GetProcessHeap(),0,owner); return 1; }
    }
    if(!identities(&owner->impl,level,generation)) return 0;
    __try { for(auto &s:owner->impl.slots) if(s.serial) clear_slot(&owner->impl,s); } __except(EXCEPTION_EXECUTE_HANDLER) { owner->impl.quarantined=true; return 0; }
    if(owner->impl.lights && !doom_world_lights_destroy(owner->impl.lights,level,generation)) return 0;
    HeapFree(GetProcessHeap(),0,owner); return 1;
}
#else
struct DoomWorldRenderer {};
extern "C" DoomWorldRenderer *doom_world_renderer_create(void *,void *,uint32_t,int) { return nullptr; }
extern "C" uint32_t doom_world_renderer_add(DoomWorldRenderer *,void *,uint32_t,const uint8_t *,unsigned,const float *,float,unsigned) { return 0; }
extern "C" int doom_world_renderer_update(DoomWorldRenderer *,void *,uint32_t,uint32_t,const uint8_t *,unsigned) { return 0; }
extern "C" int doom_world_renderer_move(DoomWorldRenderer *,void *,uint32_t,uint32_t,const float *) { return 0; }
extern "C" int doom_world_renderer_remove(DoomWorldRenderer *,void *,uint32_t,uint32_t) { return 0; }
extern "C" int doom_world_renderer_destroy(DoomWorldRenderer *,void *,uint32_t) { return 1; }
#endif
