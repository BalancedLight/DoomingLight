#include "doom_damage_direction.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>

namespace {
struct Vec3 { float x,y,z; };
using GetVector = Vec3 *(__fastcall *)(void *,Vec3 *);
using GetLevel = void *(__fastcall *)(void *);
using GetId = int (__fastcall *)(void *);
using GetLevelObject = void *(__fastcall *)(void *,int);
using Cast = void *(__fastcall *)(void *,long,const void *,const void *,int);
using Damage = void (__fastcall *)(void *,void *);
thread_local int active_side=0;
thread_local DoomEnvironmentDamageKind active_environment=DOOM_ENVIRONMENT_OTHER;
thread_local void *active_receiver=nullptr;
thread_local unsigned hook_depth=0;
}
struct DoomDamageDirection {
    unsigned char *game;
    DoomDamageViewFn view;
    DoomDamageViewReleaseFn release;
    void *context;
    GetVector position, forward, left;
    GetLevel level;
    GetId id;
    GetLevelObject object;
    Cast cast;
    Damage original;
    void **slot;
    std::atomic<unsigned> hook_calls{0}, local_calls{0}, side_reads{0}, matched_reads{0};
    std::atomic<int> last_reason{DOOM_DIRECTION_NONE}, last_side{0};
};
namespace {
std::atomic<DoomDamageDirection *> current{nullptr};
std::atomic<Damage> fallback{nullptr};
SRWLOCK hook_lock=SRWLOCK_INIT;
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
int reason(DoomDamageDirection *d,int why,int side=0) {
    d->last_reason.store(why,std::memory_order_relaxed);
    d->last_side.store(side,std::memory_order_relaxed);
    return side;
}
int bearing_with_view(DoomDamageDirection *d,void *receiver,void *info,void *player,void *camera) {
    __try {
        if (receiver!=static_cast<unsigned char *>(player)+24) return reason(d,DOOM_DIRECTION_OTHER_RECEIVER);
        d->local_calls.fetch_add(1,std::memory_order_relaxed);
        void *source=*reinterpret_cast<void **>(static_cast<unsigned char *>(info)+8);
        if (!source) return reason(d,DOOM_DIRECTION_NO_SOURCE);
        if (source==static_cast<unsigned char *>(player)+40) return reason(d,DOOM_DIRECTION_SELF);
        void *actor=d->cast(source,0,d->game+0x1C952A8,d->game+0x1CA9818,0);
        if (!actor || static_cast<unsigned char *>(actor)+40!=source) return reason(d,DOOM_DIRECTION_NOT_ACTOR);
        void *level=d->level(static_cast<unsigned char *>(player)+40);
        if (!level || d->level(source)!=level) return reason(d,DOOM_DIRECTION_WRONG_LEVEL);
        int id=d->id(source);
        if (id<0 || d->object(level,id)!=source) return reason(d,DOOM_DIRECTION_STALE_ID);
        Vec3 p{},a{},f{},l{};
        if (!d->position(static_cast<unsigned char *>(player)+24,&p) ||
            !d->position(static_cast<unsigned char *>(actor)+24,&a) ||
            !d->forward(camera,&f) || !d->left(camera,&l) ||
            !finite(p)||!finite(a)||!finite(f)||!finite(l)) return reason(d,DOOM_DIRECTION_BAD_VECTORS);
        f={-f.x,-f.y,-f.z};
        l={-l.x,-l.y,-l.z};
        float flen=std::sqrt(f.x*f.x+f.z*f.z), llen=std::sqrt(l.x*l.x+l.z*l.z);
        float dx=a.x-p.x,dz=a.z-p.z;
        if (flen<0.0001f || llen<0.0001f || dx*dx+dz*dz<0.0001f) return reason(d,DOOM_DIRECTION_DEGENERATE);
        float front=(dx*f.x+dz*f.z)/flen, lateral=(dx*l.x+dz*l.z)/llen;
        if (front>0 && std::fabs(lateral)<front) return reason(d,DOOM_DIRECTION_FRONT);
        return lateral>0 ? reason(d,DOOM_DIRECTION_LEFT,-1) : lateral<0 ? reason(d,DOOM_DIRECTION_RIGHT,1) : reason(d,DOOM_DIRECTION_DEGENERATE);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return reason(d,DOOM_DIRECTION_EXCEPTION); }
}
int bearing(DoomDamageDirection *d,void *receiver,void *info) {
    if (!info) return reason(d,DOOM_DIRECTION_NO_INFO);
    void *player=nullptr,*camera=nullptr;
    if (!d->view(d->context,&player,&camera)) return reason(d,DOOM_DIRECTION_NO_VIEW);
    const int side=player && camera ? bearing_with_view(d,receiver,info,player,camera)
                                    : reason(d,DOOM_DIRECTION_NO_VIEW);
    d->release(d->context);
    return side;
}
DoomEnvironmentDamageKind environment(void *info) {
    __try {
        if (!info) return DOOM_ENVIRONMENT_OTHER;
        const int type=*reinterpret_cast<const int *>(static_cast<const unsigned char *>(info)+60);
        if (type==11) return DOOM_ENVIRONMENT_FIRE;
        if (type==57) return DOOM_ENVIRONMENT_ACID;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return DOOM_ENVIRONMENT_OTHER;
}
void __fastcall damage_hook(void *receiver,void *info) {
    if (!hook_depth) AcquireSRWLockShared(&hook_lock);
    ++hook_depth;
    DoomDamageDirection *d=current.load(std::memory_order_acquire);
    if (!d) {
        Damage original=fallback.load(std::memory_order_acquire);
        if (!--hook_depth) ReleaseSRWLockShared(&hook_lock);
        if (original) original(receiver,info);
        return;
    }
    d->hook_calls.fetch_add(1,std::memory_order_relaxed);
    int previous=active_side;
    DoomEnvironmentDamageKind previous_environment=active_environment;
    void *previous_receiver=active_receiver;
    active_receiver=receiver;
    active_side=bearing(d,receiver,info);
    active_environment=environment(info);
    d->original(receiver,info);
    active_side=previous;
    active_environment=previous_environment;
    active_receiver=previous_receiver;
    if (!--hook_depth) ReleaseSRWLockShared(&hook_lock);
}
}
extern "C" DoomDamageDirection *doom_damage_direction_start(void *game,void *engine,DoomDamageViewFn view,
                                                            DoomDamageViewReleaseFn release,void *context) {
    if (!game||!engine||!view||!release||current.load(std::memory_order_acquire)) return nullptr;
    auto *g=static_cast<unsigned char *>(game); auto module=static_cast<HMODULE>(engine);
    static const unsigned char prefix[]={0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x41,0x54,0x41,0x57,0x48,0x8d,0x6c,0x24,0x98};
    static const unsigned char cast_prefix[]={0xff,0x25,0x64,0x19,0x0c,0x00};
    if (std::memcmp(g+0xB16AF0,prefix,sizeof(prefix)) ||
        std::memcmp(g+0x130A87E,cast_prefix,sizeof(cast_prefix)) ||
        std::strcmp(reinterpret_cast<const char *>(g+0x1C952A8+16),".?AVIGSObject@@") ||
        std::strcmp(reinterpret_cast<const char *>(g+0x1CA9818+16),".?AVActorDI@@")) return nullptr;
    auto *d=new(std::nothrow) DoomDamageDirection{}; if (!d) return nullptr;
    d->game=g; d->view=view; d->release=release; d->context=context; d->slot=reinterpret_cast<void **>(g+0x17DE250);
    d->original=reinterpret_cast<Damage>(g+0xB16AF0);
    d->position=reinterpret_cast<GetVector>(GetProcAddress(module,"?GetWorldPosition@IControlObject@@QEBA?AVvec3@@XZ"));
    d->forward=reinterpret_cast<GetVector>(GetProcAddress(module,"?GetForwardVector@IBaseCamera@@QEBA?BVvec3@@XZ"));
    d->left=reinterpret_cast<GetVector>(GetProcAddress(module,"?GetLeftVector@IBaseCamera@@QEBA?BVvec3@@XZ"));
    d->level=reinterpret_cast<GetLevel>(GetProcAddress(module,"?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
    d->id=reinterpret_cast<GetId>(GetProcAddress(module,"?GetID@IGSObject@@QEBAHXZ"));
    d->object=reinterpret_cast<GetLevelObject>(GetProcAddress(module,"?GetObjectByID@ILevel@@QEBAPEAVIGSObject@@H@Z"));
    d->cast=reinterpret_cast<Cast>(g+0x130A87E);
    if (!d->position||!d->forward||!d->left||!d->level||!d->id||!d->object||*d->slot!=reinterpret_cast<void *>(d->original)) { delete d; return nullptr; }
    DWORD old=0; if (!VirtualProtect(d->slot,sizeof(void *),PAGE_READWRITE,&old)) { delete d; return nullptr; }
    fallback.store(d->original,std::memory_order_release);
    current.store(d,std::memory_order_release);
    void *seen=InterlockedCompareExchangePointer(d->slot,reinterpret_cast<void *>(&damage_hook),reinterpret_cast<void *>(d->original));
    DWORD ignored; VirtualProtect(d->slot,sizeof(void *),old,&ignored);
    if (seen!=reinterpret_cast<void *>(d->original)) { current.store(nullptr,std::memory_order_release); delete d; return nullptr; }
    return d;
}
extern "C" int doom_damage_direction_side(const DoomDamageDirection *d,const void *player) {
    if (!d) return 0;
    const_cast<DoomDamageDirection *>(d)->side_reads.fetch_add(1,std::memory_order_relaxed);
    if (player && active_receiver==static_cast<const unsigned char *>(player)+24 &&
        current.load(std::memory_order_acquire)==d) {
        const_cast<DoomDamageDirection *>(d)->matched_reads.fetch_add(1,std::memory_order_relaxed);
        return active_side;
    }
    return 0;
}
extern "C" DoomEnvironmentDamageKind doom_damage_direction_environment(const DoomDamageDirection *d,const void *player) {
    if (d && player && active_receiver==static_cast<const unsigned char *>(player)+24 &&
        current.load(std::memory_order_acquire)==d) return active_environment;
    return DOOM_ENVIRONMENT_OTHER;
}
extern "C" int doom_damage_direction_snapshot(const DoomDamageDirection *d,DoomDamageDirectionSnapshot *s) {
    if (!d || !s) return 0;
    s->hook_calls=d->hook_calls.load(std::memory_order_relaxed);
    s->local_calls=d->local_calls.load(std::memory_order_relaxed);
    s->side_reads=d->side_reads.load(std::memory_order_relaxed);
    s->matched_reads=d->matched_reads.load(std::memory_order_relaxed);
    s->last_reason=d->last_reason.load(std::memory_order_relaxed);
    s->last_side=d->last_side.load(std::memory_order_relaxed);
    return 1;
}
extern "C" int doom_damage_direction_stop(DoomDamageDirection *d) {
    if (!d || current.load(std::memory_order_acquire)!=d) return 0;
    DWORD old=0; if (!VirtualProtect(d->slot,sizeof(void *),PAGE_READWRITE,&old)) return 0;
    void *seen=InterlockedCompareExchangePointer(d->slot,reinterpret_cast<void *>(d->original),reinterpret_cast<void *>(&damage_hook));
    DWORD ignored; BOOL restored=VirtualProtect(d->slot,sizeof(void *),old,&ignored);
    if ((seen!=reinterpret_cast<void *>(&damage_hook) && seen!=reinterpret_cast<void *>(d->original)) || !restored) return 0;
    /* Never wait on a callback that may need the caller's engine mutex. */
    if (hook_depth || !TryAcquireSRWLockExclusive(&hook_lock)) return 0;
    if (current.load(std::memory_order_acquire)!=d) { ReleaseSRWLockExclusive(&hook_lock); return 0; }
    current.store(nullptr,std::memory_order_release);
    delete d; ReleaseSRWLockExclusive(&hook_lock); return 1;
}
