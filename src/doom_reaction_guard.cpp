#include "doom_reaction_guard.h"
#include "doom_hostility.h"

#if defined(DOOM_MODE_RUNTIME) && defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <new>

namespace {
using GetPlayer = void *(__fastcall *)(void *);
using GetLevel = void *(__fastcall *)(void *);
using GetId = int (__fastcall *)(void *);
using GetLevelObject = void *(__fastcall *)(void *, int);
using DynamicCast = void *(__fastcall *)(void *, long, const void *, const void *, int);
using CanBeGrabbed = unsigned char (__fastcall *)(void *, void *, unsigned);
using ProcessKnockdown = uintptr_t (__fastcall *)(void *, void *);
using Knockdown = uintptr_t (__fastcall *)(void *, float, void *);

std::atomic<DoomReactionGuard *> current{nullptr};
std::atomic<CanBeGrabbed> grab_original{nullptr};
std::atomic<ProcessKnockdown> process_original{nullptr};
std::atomic<Knockdown> knock_original{nullptr};
SRWLOCK lifecycle_lock=SRWLOCK_INIT;
SRWLOCK calls_lock=SRWLOCK_INIT;
DoomReactionGuard *retained=nullptr;
bool claimed=false;

template<class T> T field(const void *p, size_t offset) {
    return *reinterpret_cast<const T *>(static_cast<const unsigned char *>(p)+offset);
}
}

struct DoomReactionGuard {
    unsigned char *game=nullptr;
    HMODULE engine=nullptr;
    GetPlayer player=nullptr;
    GetLevel level=nullptr;
    GetId id=nullptr;
    GetLevelObject object=nullptr;
    DynamicCast cast=nullptr;
    std::atomic<void *> expected_level{nullptr};
    std::atomic<uint32_t> generation{0};
    std::atomic<bool> active{false};
    bool process_created=false, knock_created=false;
    bool process_enabled=false, knock_enabled=false, grab_installed=false;
    bool published=false;
};

namespace {
bool gameplay_and_player(DoomReactionGuard *g, void *player, void **level_out) {
    if (!g || !player || !level_out || !g->active.load(std::memory_order_acquire) ||
        !g->generation.load(std::memory_order_acquire)) return false;
    void *game=field<void *>(g->game,0x1d07f88);
    if (!game || field<void *>(game,0)!=g->game+0x157d5f8 ||
        g->player(game)!=player || field<void *>(player,0)!=g->game+0x17dd738 ||
        field<void *>(static_cast<unsigned char *>(player)+40,0)!=g->game+0x17de468) return false;
    void *input=field<void *>(game,200), *session=field<void *>(game,1288);
    void *manager=session ? field<void *>(session,192) : nullptr;
    if (!input || field<int>(input,308)!=0 || !manager || !field<void *>(manager,64) ||
        (field<int>(manager,88)!=1 && field<int>(manager,88)!=2)) return false;
    void *level=g->level(static_cast<unsigned char *>(player)+40);
    if (!level || level!=g->expected_level.load(std::memory_order_acquire) ||
        g->player(game)!=player) return false;
    *level_out=level;
    return true;
}
bool hostile_object(DoomReactionGuard *g, void *player, void *object) {
    if (!g || !player || !object || object==player) return false;
    void *level=nullptr;
    if (!gameplay_and_player(g,player,&level)) return false;
    void *source=static_cast<unsigned char *>(object)+40;
    if (g->level(source)!=level) return false;
    const int id=g->id(source);
    if (id<0 || g->object(level,id)!=source) return false;
    if (doom_hostility_query(g->game,object,player)!=1) return false;
    void *game=field<void *>(g->game,0x1d07f88);
    return g->active.load(std::memory_order_acquire) && game && g->player(game)==player &&
        g->level(static_cast<unsigned char *>(player)+40)==level &&
        g->object(level,id)==source;
}
bool hostile_actor(DoomReactionGuard *g, void *player, void *actor) {
    if (!g || !actor) return false;
    void *object=g->cast(actor,0,g->game+0x1ca9818,g->game+0x1c95390,0);
    return object && hostile_object(g,player,object);
}
bool hostile_damage(DoomReactionGuard *g, void *player, void *damage_info) {
    if (!g || !damage_info) return false;
    void *source=field<void *>(damage_info,8);
    if (!source || source==static_cast<unsigned char *>(player)+40) return false;
    void *object=g->cast(source,0,g->game+0x1c952a8,g->game+0x1c95390,0);
    return object && static_cast<unsigned char *>(object)+40==source && hostile_object(g,player,object);
}
bool reject_grab(DoomReactionGuard *g, void *player, void *attacker, unsigned type) {
    if (type>6) return false; /* The engine reserves 7–16 for special grab states. */
    __try { return hostile_actor(g,player,attacker); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool reject_damage(DoomReactionGuard *g, void *player, void *damage_info) {
    __try { return hostile_damage(g,player,damage_info); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
unsigned char __fastcall grab_hook(void *player, void *attacker, unsigned type) {
    bool reject=false;
    if (TryAcquireSRWLockShared(&calls_lock)) {
        DoomReactionGuard *g=current.load(std::memory_order_acquire);
        reject=reject_grab(g,player,attacker,type);
        ReleaseSRWLockShared(&calls_lock);
    }
    CanBeGrabbed original=grab_original.load(std::memory_order_acquire);
    return reject ? 0 : original ? original(player,attacker,type) : 0;
}
uintptr_t __fastcall process_hook(void *player, void *info) {
    bool reject=false;
    if (TryAcquireSRWLockShared(&calls_lock)) {
        DoomReactionGuard *g=current.load(std::memory_order_acquire);
        reject=reject_damage(g,player,info);
        ReleaseSRWLockShared(&calls_lock);
    }
    ProcessKnockdown original=process_original.load(std::memory_order_acquire);
    return reject ? 0 : original ? original(player,info) : 0;
}
uintptr_t __fastcall knock_hook(void *player, float duration, void *info) {
    const void *caller=_ReturnAddress();
    bool reject=false;
    if (TryAcquireSRWLockShared(&calls_lock)) {
        DoomReactionGuard *g=current.load(std::memory_order_acquire);
        if (g && caller==g->game+0x27e632) reject=reject_damage(g,player,info);
        ReleaseSRWLockShared(&calls_lock);
    }
    Knockdown original=knock_original.load(std::memory_order_acquire);
    return reject ? 0 : original ? original(player,duration,info) : 0;
}
bool exchange_slot(void **slot, void *expected, void *replacement, bool *changed) {
    DWORD old=0;
    *changed=false;
    if (!VirtualProtect(slot,sizeof(void *),PAGE_READWRITE,&old)) return false;
    void *seen=InterlockedCompareExchangePointer(slot,replacement,expected);
    *changed=seen==expected;
    DWORD ignored=0;
    return VirtualProtect(slot,sizeof(void *),old,&ignored)!=0 && *changed;
}
bool disable_one(void *address, bool *enabled) {
    if (!*enabled) return true;
    const MH_STATUS result=MH_DisableHook(address);
    if (result!=MH_OK && result!=MH_ERROR_DISABLED) return false;
    *enabled=false;
    return true;
}
bool stop_locked(DoomReactionGuard *g) {
    g->active.store(false,std::memory_order_release);
    bool ok=true;
    if (g->grab_installed) {
        auto **slot=reinterpret_cast<void **>(g->game+0x17dda40);
        DWORD old=0;
        if (!VirtualProtect(slot,sizeof(void *),PAGE_READWRITE,&old)) ok=false;
        else {
            void *seen=InterlockedCompareExchangePointer(slot,g->game+0xb0f100,
                reinterpret_cast<void *>(&grab_hook));
            DWORD ignored=0;
            const bool restored=VirtualProtect(slot,sizeof(void *),old,&ignored)!=0;
            if (restored && (seen==reinterpret_cast<void *>(&grab_hook) || seen==g->game+0xb0f100))
                g->grab_installed=false;
            else ok=false;
        }
    }
    if (!disable_one(g->game+0xb18560,&g->knock_enabled)) ok=false;
    if (!disable_one(g->game+0xb586d0,&g->process_enabled)) ok=false;
    if (!ok || !TryAcquireSRWLockExclusive(&calls_lock)) return false;
    DoomReactionGuard *expected=g;
    current.compare_exchange_strong(expected,nullptr,std::memory_order_acq_rel);
    g->expected_level.store(nullptr,std::memory_order_release);
    g->generation.store(0,std::memory_order_release);
    ReleaseSRWLockExclusive(&calls_lock);
    return true;
}
}

extern "C" DoomReactionGuard *doom_reaction_guard_create(void *game_module, void *engine_module) {
    auto *game=static_cast<unsigned char *>(game_module);
    auto engine=static_cast<HMODULE>(engine_module);
    static const unsigned char grab_prefix[]={0x40,0x56,0x57,0x41,0x55,0x48,0x83,0xec,0x50};
    static const unsigned char process_prefix[]={0x48,0x89,0x5c,0x24,0x10,0x56,0x48,0x83,0xec,0x40};
    static const unsigned char knock_prefix[]={0x48,0x89,0x5c,0x24,0x10,0x56,0x48,0x81,0xec,0xa0,0x00,0x00,0x00};
    static const unsigned char explosion_call[]={0xe8,0x2e,0x9f,0x89,0x00};
    static const unsigned char cast_prefix[]={0xff,0x25,0x64,0x19,0x0c,0x00};
    if (!game || !engine || GetModuleHandleW(L"gamedll_x64_rwdi.dll")!=game_module ||
        GetModuleHandleW(L"engine_x64_rwdi.dll")!=engine_module) return nullptr;
    __try {
        if (std::memcmp(game+0xb0f100,grab_prefix,sizeof(grab_prefix)) ||
            std::memcmp(game+0xb586d0,process_prefix,sizeof(process_prefix)) ||
            std::memcmp(game+0xb18560,knock_prefix,sizeof(knock_prefix)) ||
            std::memcmp(game+0x27e62d,explosion_call,sizeof(explosion_call)) ||
            std::memcmp(game+0x130a87e,cast_prefix,sizeof(cast_prefix)) ||
            std::strcmp(reinterpret_cast<const char *>(game+0x1c952a8+16),".?AVIGSObject@@") ||
            std::strcmp(reinterpret_cast<const char *>(game+0x1c95390+16),".?AVObjectDI@@") ||
            std::strcmp(reinterpret_cast<const char *>(game+0x1ca9818+16),".?AVActorDI@@") ||
            *reinterpret_cast<void **>(game+0x17dda40)!=game+0xb0f100) return nullptr;
        auto level=reinterpret_cast<GetLevel>(GetProcAddress(engine,"?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
        auto id=reinterpret_cast<GetId>(GetProcAddress(engine,"?GetID@IGSObject@@QEBAHXZ"));
        auto object=reinterpret_cast<GetLevelObject>(GetProcAddress(engine,"?GetObjectByID@ILevel@@QEBAPEAVIGSObject@@H@Z"));
        if (!level || !id || !object) return nullptr;
        AcquireSRWLockExclusive(&lifecycle_lock);
        if (claimed || (retained && (retained->game!=game || retained->engine!=engine))) {
            ReleaseSRWLockExclusive(&lifecycle_lock); return nullptr;
        }
        auto *g=retained;
        if (!g) {
            g=new (std::nothrow) DoomReactionGuard;
            if (g) {
                g->game=game; g->engine=engine; g->player=reinterpret_cast<GetPlayer>(game+0x3c2d90);
                g->level=level; g->id=id; g->object=object;
                g->cast=reinterpret_cast<DynamicCast>(game+0x130a87e);
                retained=g;
            }
        }
        if (g) claimed=true;
        ReleaseSRWLockExclusive(&lifecycle_lock);
        return g;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
extern "C" int doom_reaction_guard_start(DoomReactionGuard *g, void *level, uint32_t generation) {
    if (!g || !level || !generation) return 0;
    AcquireSRWLockExclusive(&lifecycle_lock);
    if (g->active.load(std::memory_order_acquire)) {
        const bool same=g->expected_level.load(std::memory_order_acquire)==level &&
                        g->generation.load(std::memory_order_acquire)==generation;
        ReleaseSRWLockExclusive(&lifecycle_lock); return same ? 1 : 0;
    }
    if (current.load(std::memory_order_acquire) && !stop_locked(g)) {
        ReleaseSRWLockExclusive(&lifecycle_lock); return 0;
    }
    MH_STATUS status=MH_Initialize();
    if (status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED) { ReleaseSRWLockExclusive(&lifecycle_lock); return 0; }
    if (!g->process_created) {
        void *original=nullptr;
        status=MH_CreateHook(g->game+0xb586d0,reinterpret_cast<void *>(&process_hook),&original);
        if (status!=MH_OK) { ReleaseSRWLockExclusive(&lifecycle_lock); return 0; }
        process_original.store(reinterpret_cast<ProcessKnockdown>(original),std::memory_order_release);
        g->process_created=true;
    }
    if (!g->knock_created) {
        void *original=nullptr;
        status=MH_CreateHook(g->game+0xb18560,reinterpret_cast<void *>(&knock_hook),&original);
        if (status!=MH_OK) { ReleaseSRWLockExclusive(&lifecycle_lock); return 0; }
        knock_original.store(reinterpret_cast<Knockdown>(original),std::memory_order_release);
        g->knock_created=true;
    }
    HMODULE self=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&grab_hook),&self)) {
        ReleaseSRWLockExclusive(&lifecycle_lock); return 0;
    }
    g->published=true;
    grab_original.store(reinterpret_cast<CanBeGrabbed>(g->game+0xb0f100),std::memory_order_release);
    g->expected_level.store(level,std::memory_order_release);
    g->generation.store(generation,std::memory_order_release);
    current.store(g,std::memory_order_release);
    status=MH_EnableHook(g->game+0xb586d0);
    g->process_enabled=status==MH_OK || status==MH_ERROR_ENABLED;
    if (g->process_enabled) {
        status=MH_EnableHook(g->game+0xb18560);
        g->knock_enabled=status==MH_OK || status==MH_ERROR_ENABLED;
    }
    bool changed=false;
    const bool swapped=g->knock_enabled && exchange_slot(reinterpret_cast<void **>(g->game+0x17dda40),
        g->game+0xb0f100,reinterpret_cast<void *>(&grab_hook),&changed);
    g->grab_installed=changed;
    if (!swapped) {
        (void)stop_locked(g);
        ReleaseSRWLockExclusive(&lifecycle_lock); return 0;
    }
    g->active.store(true,std::memory_order_release);
    ReleaseSRWLockExclusive(&lifecycle_lock);
    return 1;
}
extern "C" void doom_reaction_guard_set_active(DoomReactionGuard *g, int value) {
    if (g && current.load(std::memory_order_acquire)==g)
        g->active.store(value!=0,std::memory_order_release);
}
extern "C" int doom_reaction_guard_stop(DoomReactionGuard *g) {
    if (!g) return 1;
    AcquireSRWLockExclusive(&lifecycle_lock);
    const bool ok=stop_locked(g);
    ReleaseSRWLockExclusive(&lifecycle_lock);
    return ok ? 1 : 0;
}
extern "C" void doom_reaction_guard_destroy(DoomReactionGuard *g) {
    if (!g || !doom_reaction_guard_stop(g)) return;
    AcquireSRWLockExclusive(&lifecycle_lock);
    claimed=false;
    if (!g->published) {
        if (g->knock_created) (void)MH_RemoveHook(g->game+0xb18560);
        if (g->process_created) (void)MH_RemoveHook(g->game+0xb586d0);
        retained=nullptr; delete g;
    }
    ReleaseSRWLockExclusive(&lifecycle_lock);
}
#else
struct DoomReactionGuard {};
extern "C" DoomReactionGuard *doom_reaction_guard_create(void *, void *) { return nullptr; }
extern "C" int doom_reaction_guard_start(DoomReactionGuard *, void *, uint32_t) { return 0; }
extern "C" void doom_reaction_guard_set_active(DoomReactionGuard *, int) {}
extern "C" int doom_reaction_guard_stop(DoomReactionGuard *) { return 1; }
extern "C" void doom_reaction_guard_destroy(DoomReactionGuard *) {}
#endif
