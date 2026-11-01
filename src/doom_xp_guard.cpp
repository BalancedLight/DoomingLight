#include "doom_xp_guard.h"

#if defined(DOOM_MODE_RUNTIME) && defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <new>

namespace {
using GetPlayer = void *(__fastcall *)(void *);
using GetLevel = void *(__fastcall *)(void *);
using Award = void (__fastcall *)(void *, int, int, unsigned char, void **, unsigned char, unsigned char);
std::atomic<DoomXpGuard *> current{nullptr};
std::atomic<Award> original{nullptr};
SRWLOCK lifecycle_lock=SRWLOCK_INIT;
SRWLOCK calls_lock=SRWLOCK_INIT;
DoomXpGuard *retained=nullptr;
bool claimed=false;
template<class T> T field(const void *p, size_t offset) {
    return *reinterpret_cast<const T *>(static_cast<const unsigned char *>(p)+offset);
}
}

struct DoomXpGuard {
    unsigned char *game=nullptr;
    HMODULE engine=nullptr;
    GetPlayer player=nullptr;
    GetLevel level=nullptr;
    std::atomic<void *> expected_level{nullptr};
    std::atomic<uint32_t> generation{0};
    std::atomic<bool> active{false};
    bool created=false, enabled=false, published=false;
};

namespace {
bool is_local_player(DoomXpGuard *g, void *actor) {
    __try {
        if (!g || !actor || !g->active.load(std::memory_order_acquire) ||
            !g->generation.load(std::memory_order_acquire)) return false;
        void *game=field<void *>(g->game,0x1d07f88);
        if (!game || field<void *>(game,0)!=g->game+0x157d5f8 ||
            g->player(game)!=actor || field<void *>(actor,0)!=g->game+0x17dd738 ||
            field<void *>(actor,40)!=g->game+0x17de468) return false;
        void *level=g->level(static_cast<unsigned char *>(actor)+40);
        return level && level==g->expected_level.load(std::memory_order_acquire) &&
            g->active.load(std::memory_order_acquire) && g->player(game)==actor;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __fastcall award_hook(void *actor, int amount, int type, unsigned char notify,
                            void **message, unsigned char bonus, unsigned char scale) {
    bool suppress=false;
    if (amount>0 && TryAcquireSRWLockShared(&calls_lock)) {
        suppress=is_local_player(current.load(std::memory_order_acquire),actor);
        ReleaseSRWLockShared(&calls_lock);
    }
    Award next=original.load(std::memory_order_acquire);
    /* The zero-amount native path still frees the caller-owned message buffer. */
    if (next) next(actor,suppress ? 0 : amount,type,notify,message,bonus,scale);
}
bool stop_locked(DoomXpGuard *g) {
    g->active.store(false,std::memory_order_release);
    if (g->enabled) {
        const MH_STATUS status=MH_DisableHook(g->game+0xb234f0);
        if (status!=MH_OK && status!=MH_ERROR_DISABLED) return false;
        g->enabled=false;
    }
    if (!TryAcquireSRWLockExclusive(&calls_lock)) return false;
    DoomXpGuard *expected=g;
    current.compare_exchange_strong(expected,nullptr,std::memory_order_acq_rel);
    g->expected_level.store(nullptr,std::memory_order_release);
    g->generation.store(0,std::memory_order_release);
    ReleaseSRWLockExclusive(&calls_lock);
    return true;
}
}

extern "C" DoomXpGuard *doom_xp_guard_create(void *game_module, void *engine_module) {
    if (!game_module || !engine_module) return nullptr;
    __try {
        auto *game=static_cast<unsigned char *>(game_module);
        auto engine=static_cast<HMODULE>(engine_module);
        static const unsigned char prefix[]={0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x50};
        if (std::memcmp(game+0xb234f0,prefix,sizeof(prefix))) return nullptr;
        auto level=reinterpret_cast<GetLevel>(GetProcAddress(engine,"?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
        if (!level) return nullptr;
        AcquireSRWLockExclusive(&lifecycle_lock);
        if (claimed || (retained && (retained->game!=game || retained->engine!=engine))) {
            ReleaseSRWLockExclusive(&lifecycle_lock); return nullptr;
        }
        auto *g=retained;
        if (!g) {
            g=new (std::nothrow) DoomXpGuard;
            if (g) {
                g->game=game; g->engine=engine;
                g->player=reinterpret_cast<GetPlayer>(game+0x3c2d90);
                g->level=level; retained=g;
            }
        }
        if (g) claimed=true;
        ReleaseSRWLockExclusive(&lifecycle_lock);
        return g;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
extern "C" int doom_xp_guard_start(DoomXpGuard *g, void *level, uint32_t generation) {
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
    if (!g->created) {
        void *next=nullptr;
        status=MH_CreateHook(g->game+0xb234f0,reinterpret_cast<void *>(&award_hook),&next);
        if (status!=MH_OK) { ReleaseSRWLockExclusive(&lifecycle_lock); return 0; }
        original.store(reinterpret_cast<Award>(next),std::memory_order_release);
        g->created=true;
    }
    HMODULE self=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&award_hook),&self)) {
        ReleaseSRWLockExclusive(&lifecycle_lock); return 0;
    }
    g->published=true;
    g->expected_level.store(level,std::memory_order_release);
    g->generation.store(generation,std::memory_order_release);
    current.store(g,std::memory_order_release);
    status=MH_EnableHook(g->game+0xb234f0);
    g->enabled=status==MH_OK || status==MH_ERROR_ENABLED;
    if (!g->enabled) {
        (void)stop_locked(g);
        ReleaseSRWLockExclusive(&lifecycle_lock); return 0;
    }
    g->active.store(true,std::memory_order_release);
    ReleaseSRWLockExclusive(&lifecycle_lock);
    return 1;
}
extern "C" void doom_xp_guard_set_active(DoomXpGuard *g, int active) {
    if (g && current.load(std::memory_order_acquire)==g)
        g->active.store(active!=0,std::memory_order_release);
}
extern "C" int doom_xp_guard_stop(DoomXpGuard *g) {
    if (!g) return 1;
    AcquireSRWLockExclusive(&lifecycle_lock);
    const bool ok=stop_locked(g);
    ReleaseSRWLockExclusive(&lifecycle_lock);
    return ok ? 1 : 0;
}
extern "C" void doom_xp_guard_destroy(DoomXpGuard *g) {
    if (!g || !doom_xp_guard_stop(g)) return;
    AcquireSRWLockExclusive(&lifecycle_lock);
    claimed=false;
    if (!g->published) {
        if (g->created) (void)MH_RemoveHook(g->game+0xb234f0);
        retained=nullptr; delete g;
    }
    ReleaseSRWLockExclusive(&lifecycle_lock);
}
#else
struct DoomXpGuard {};
extern "C" DoomXpGuard *doom_xp_guard_create(void *, void *) { return nullptr; }
extern "C" int doom_xp_guard_start(DoomXpGuard *, void *, uint32_t) { return 0; }
extern "C" void doom_xp_guard_set_active(DoomXpGuard *, int) {}
extern "C" int doom_xp_guard_stop(DoomXpGuard *) { return 1; }
extern "C" void doom_xp_guard_destroy(DoomXpGuard *) {}
#endif
