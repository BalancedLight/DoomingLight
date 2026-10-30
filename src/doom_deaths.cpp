#include "doom_deaths.h"
#include "doom_hostility.h"
#if defined(DOOM_MODE_LAB) && defined(DOOM_LAB_HOSTILITY) && defined(_DEBUG) && !defined(NDEBUG)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <new>

namespace {
struct Vec3 { float x,y,z; };
using Dispatch=void (__fastcall *)(const void *);
using GetPlayer=void *(__fastcall *)(void *);
using GetLevel=void *(__fastcall *)(void *);
using GetId=int (__fastcall *)(void *);
using GetLevelObject=void *(__fastcall *)(void *,int);
using GetPosition=Vec3 *(__fastcall *)(void *,Vec3 *);
using DynamicCast=void *(__fastcall *)(void *,long,const void *,const void *,int);
struct Event { uint32_t generation; int id; };
}
struct DoomDeaths {
    unsigned char *game=nullptr;
    GetPlayer get_player=nullptr; GetLevel get_level=nullptr; GetId get_id=nullptr;
    GetLevelObject get_object=nullptr; GetPosition get_position=nullptr; DynamicCast cast=nullptr;
    std::recursive_mutex mutex;
    void *player=nullptr, *level=nullptr;
    uint32_t generation=0;
    std::array<Event,128> pending{};
    unsigned pending_count=0;
    std::array<uint64_t,256> delivered{};
    unsigned delivered_next=0;
    bool enabled=false, published=false, created=false;
};
namespace {
std::atomic<DoomDeaths *> current{nullptr};
Dispatch original=nullptr;
void *local_player(DoomDeaths *d) {
    void *game=*reinterpret_cast<void **>(d->game+0x1D07F88);
    return game ? d->get_player(game) : nullptr;
}
int capture(DoomDeaths *d, const void *signal) {
    __try {
        if (!signal || !d->enabled || !d->player || !d->level) return 0;
        void *victim=reinterpret_cast<void *const *>(signal)[0];
        void *killer=reinterpret_cast<void *const *>(signal)[1];
        if (!victim || killer!=d->player || local_player(d)!=d->player) return 0;
        auto *victim_gs=static_cast<unsigned char *>(victim)+40;
        auto *player_gs=static_cast<unsigned char *>(d->player)+40;
        if (d->get_level(player_gs)!=d->level || d->get_level(victim_gs)!=d->level) return 0;
        int id=d->get_id(victim_gs);
        if (id<0 || d->get_object(d->level,id)!=victim_gs) return 0;
        for (unsigned i=0;i<d->pending_count;i++)
            if (d->pending[i].generation==d->generation && d->pending[i].id==id) return 1;
        if (d->pending_count==d->pending.size()) return 0;
        d->pending[d->pending_count++]={d->generation,id};
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void __fastcall killed_hook(const void *signal) {
    DoomDeaths *d=current.load(std::memory_order_acquire);
    if (d) {
        std::lock_guard<std::recursive_mutex> lock(d->mutex);
        (void)capture(d,signal);
    }
    original(signal);
}
int consume(DoomDeaths *d, Event item, DoomDeathRecord *out) {
    __try {
        if (!d->enabled || !d->player || !d->level || item.generation!=d->generation ||
            local_player(d)!=d->player ||
            d->get_level(static_cast<unsigned char *>(d->player)+40)!=d->level) return 0;
        void *gs=d->get_object(d->level,item.id);
        if (!gs || d->get_id(gs)!=item.id || d->get_level(gs)!=d->level) return 0;
        void *target=d->cast(gs,0,d->game+0x1C952A8,d->game+0x1C95390,0);
        if (!target || target==d->player || static_cast<unsigned char *>(target)+40!=gs ||
            doom_hostility_query(d->game,target,d->player)!=1) return 0;
        uint64_t id=(static_cast<uint64_t>(d->generation)<<32) |
                    (static_cast<uint64_t>(static_cast<uint32_t>(item.id))+1u);
        for (uint64_t old : d->delivered) if (old==id) return 0;
        Vec3 position{};
        if (d->get_position(static_cast<unsigned char *>(target)+24,&position)!=&position ||
            !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
            local_player(d)!=d->player || d->get_object(d->level,item.id)!=gs ||
            d->get_level(gs)!=d->level) return 0;
        d->delivered[d->delivered_next++ % d->delivered.size()]=id;
        out->id=id; out->position={position.x*32.0f,position.z*32.0f,position.y*32.0f};
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
}
extern "C" DoomDeaths *doom_deaths_create(void *game_module, void *engine_module) {
    auto *game=static_cast<unsigned char *>(game_module);
    HMODULE engine=static_cast<HMODULE>(engine_module);
    if (!game || !engine || GetModuleHandleW(L"gamedll_x64_rwdi.dll")!=game_module ||
        GetModuleHandleW(L"engine_x64_rwdi.dll")!=engine_module) return nullptr;
    static const unsigned char dispatcher[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,
                                                0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x8b};
    static const unsigned char cast_prefix[]={0xff,0x25,0x64,0x19,0x0c,0x00};
    if (std::memcmp(game+0x333980,dispatcher,sizeof(dispatcher)) ||
        std::memcmp(game+0x130A87E,cast_prefix,sizeof(cast_prefix)) ||
        std::strcmp(reinterpret_cast<const char *>(game+0x1C952A8+16),".?AVIGSObject@@") ||
        std::strcmp(reinterpret_cast<const char *>(game+0x1C95390+16),".?AVObjectDI@@")) return nullptr;
    DoomDeaths *d=new (std::nothrow) DoomDeaths;
    if (!d) return nullptr;
    d->game=game; d->get_player=reinterpret_cast<GetPlayer>(game+0x3C2D90);
    d->get_level=reinterpret_cast<GetLevel>(GetProcAddress(engine,"?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
    d->get_id=reinterpret_cast<GetId>(GetProcAddress(engine,"?GetID@IGSObject@@QEBAHXZ"));
    d->get_object=reinterpret_cast<GetLevelObject>(GetProcAddress(engine,"?GetObjectByID@ILevel@@QEBAPEAVIGSObject@@H@Z"));
    d->get_position=reinterpret_cast<GetPosition>(GetProcAddress(engine,"?GetWorldPosition@IControlObject@@QEBA?AVvec3@@XZ"));
    d->cast=reinterpret_cast<DynamicCast>(game+0x130A87E);
    if (!d->get_level || !d->get_id || !d->get_object || !d->get_position) { delete d; return nullptr; }
    return d;
}
extern "C" int doom_deaths_start(DoomDeaths *d) {
    if (!d) return 0;
    std::lock_guard<std::recursive_mutex> lock(d->mutex);
    if (d->enabled) return 1;
    DoomDeaths *expected=nullptr;
    if (!current.compare_exchange_strong(expected,d) && expected!=d) return 0;
    MH_STATUS status=MH_Initialize();
    if (status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED) { current.store(nullptr); return 0; }
    if (!d->created) {
        status=MH_CreateHook(d->game+0x333980,reinterpret_cast<void *>(&killed_hook),
                             reinterpret_cast<void **>(&original));
        if (status!=MH_OK) { current.store(nullptr); return 0; }
        d->created=true;
    }
    HMODULE self=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&killed_hook),&self)) {
        current.store(nullptr); return 0;
    }
    d->published=true;
    status=MH_EnableHook(d->game+0x333980);
    if (status!=MH_OK && status!=MH_ERROR_ENABLED) { current.store(nullptr); return 0; }
    d->enabled=true; return 1;
}
extern "C" void doom_deaths_context(DoomDeaths *d, void *player, void *level, uint32_t generation) {
    if (!d) return;
    std::lock_guard<std::recursive_mutex> lock(d->mutex);
    if (d->generation!=generation || d->level!=level || d->player!=player) {
        d->pending_count=0; d->delivered.fill(0); d->delivered_next=0;
    }
    d->generation=generation; d->player=player; d->level=level;
}
extern "C" unsigned doom_deaths_drain(DoomDeaths *d, DoomDeathRecord *out, unsigned capacity) {
    if (!d || !out || !capacity) return 0;
    if (capacity>128) capacity=128;
    std::lock_guard<std::recursive_mutex> lock(d->mutex);
    unsigned produced=0,read=0;
    while (read<d->pending_count && produced<capacity) {
        if (consume(d,d->pending[read],&out[produced])) ++produced;
        ++read;
    }
    if (read) {
        for (unsigned i=read;i<d->pending_count;i++) d->pending[i-read]=d->pending[i];
        d->pending_count-=read;
    }
    return produced;
}
extern "C" int doom_deaths_stop(DoomDeaths *d) {
    if (!d) return 1;
    std::lock_guard<std::recursive_mutex> lock(d->mutex);
    d->enabled=false; d->player=nullptr; d->level=nullptr; d->pending_count=0;
    if (d->created) {
        MH_STATUS status=MH_DisableHook(d->game+0x333980);
        if (status!=MH_OK && status!=MH_ERROR_DISABLED) return 0;
    }
    DoomDeaths *expected=d; current.compare_exchange_strong(expected,nullptr);
    return 1;
}
extern "C" void doom_deaths_destroy(DoomDeaths *d) {
    if (!d) return;
    (void)doom_deaths_stop(d);
    if (!d->published) {
        if (d->created) (void)MH_RemoveHook(d->game+0x333980);
        delete d;
    }
}
#else
extern "C" DoomDeaths *doom_deaths_create(void *,void *) { return nullptr; }
extern "C" int doom_deaths_start(DoomDeaths *) { return 0; }
extern "C" void doom_deaths_context(DoomDeaths *,void *,void *,uint32_t) {}
extern "C" unsigned doom_deaths_drain(DoomDeaths *,DoomDeathRecord *,unsigned) { return 0; }
extern "C" int doom_deaths_stop(DoomDeaths *) { return 1; }
extern "C" void doom_deaths_destroy(DoomDeaths *) {}
#endif
