/* Native loot conversion for the supported Player build. GPL-2.0-or-later. */
#include "doom_loot_adapter.h"
#if defined(DOOM_MODE_RUNTIME)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <new>

struct DoomLootAdapter {
    unsigned char *game;
    void *player, *level;
    void *(__fastcall *get_level)(void *);
    uint32_t generation;
    uint64_t epoch, head_revision;
    DoomLootRecord pending[64];
    unsigned count, reserved;
    bool enabled, accepting, created, trigger_created, published;
};
namespace {
using Grant = void *(__fastcall *)(void *, void *, int, unsigned char, char, int, void *);
using ItemType = int (__fastcall *)(void *);
using GetPlayer = void *(__fastcall *)(void *);
using Trigger = unsigned char (__fastcall *)(void *, unsigned long long, void *);
std::atomic<DoomLootAdapter *> current{nullptr};
SRWLOCK lock = SRWLOCK_INIT;
Grant original = nullptr;
Trigger original_trigger = nullptr;
thread_local void *container_item = nullptr;
DoomLootAdapter *retained = nullptr;
bool claimed = false;

/* Native inventory remains authoritative for quests and future maps. */
int category(int type) {
    switch (type) {
    case 2: return DOOM_LOOT_HEALTH;
    case 1: case 4: case 5: case 6: case 7: case 8: case 11: case 12: case 13: case 15: case 16: case 17:
    case 18: case 19: case 20: case 21: case 22: case 23: case 24:
    case 34: case 35: case 36: case 37: case 38: case 39: case 40:
    case 119: return DOOM_LOOT_AMMO;
    default: return 0;
    }
}
int classify(DoomLootAdapter *p, void *interface_ptr, void *item, int reason, void *caller, int *native_type) {
    __try {
        if (!p->enabled || !p->accepting || !p->player || !p->level || !p->get_level ||
            interface_ptr != static_cast<unsigned char *>(p->player)+0x950 || reason!=2 || !item) return 0;
        if (caller==p->game+0x9db036) {
            auto *owner=static_cast<unsigned char *>(container_item);
            if (!owner || *reinterpret_cast<void **>(owner)!=p->game+0x1736d18 ||
                *reinterpret_cast<void **>(owner+1032)) return 0;
        } else if (caller==p->game+0x9e9526 || caller==p->game+0x9e959e) {
            auto *owner=reinterpret_cast<unsigned char *>(reinterpret_cast<uintptr_t>(item)-920);
            if (*reinterpret_cast<void **>(owner)!=p->game+0x1739738 ||
                *reinterpret_cast<void **>(owner+1360)) return 0;
        } else if (caller!=p->game+0x9cb80e) return 0;
        void *game=*reinterpret_cast<void **>(p->game+0x1d07f88);
        auto get_player=reinterpret_cast<GetPlayer>(p->game+0x3c2d90);
        if (!game || get_player(game)!=p->player ||
            *reinterpret_cast<void **>(p->player)!=p->game+0x17dd738 ||
            p->get_level(static_cast<unsigned char *>(p->player)+40)!=p->level ||
            game!=*reinterpret_cast<void **>(p->game+0x1d07f88) || get_player(game)!=p->player ||
            p->get_level(static_cast<unsigned char *>(p->player)+40)!=p->level) return 0;
        auto *desc=static_cast<unsigned char *>(item)+24;
        if (*reinterpret_cast<void **>(desc)!=p->game+0x163d3b8 ||
            !*reinterpret_cast<void **>(desc+8)) return 0;
        auto **vtable=*reinterpret_cast<void ***>(desc);
        if (!vtable || reinterpret_cast<unsigned char (__fastcall *)(void *)>(vtable[39])(desc)) return 0;
        *native_type=reinterpret_cast<ItemType>(p->game+0x6c9840)(desc);
        return category(*native_type);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int ammo_count(unsigned char *game, void *player, void *item) {
    __try { return reinterpret_cast<int (__fastcall *)(void *, void *)>(game+0xc5e980)(player,item); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
struct CashSnapshot { void *manager, *wallet; int count; };
CashSnapshot cash_count(unsigned char *game, void *player, void *level,
                        void *(__fastcall *get_level)(void *)) {
    CashSnapshot result{nullptr,nullptr,-1};
    __try {
        auto *actor=static_cast<unsigned char *>(player);
        auto get_player=reinterpret_cast<GetPlayer>(game+0x3c2d90);
        void *host=*reinterpret_cast<void **>(game+0x1d07f88);
        if (!host || get_player(host)!=player ||
            *reinterpret_cast<void **>(actor)!=game+0x17dd738 ||
            get_level(actor+40)!=level) return result;
        auto *manager=*reinterpret_cast<unsigned char **>(actor+0x958);
        if (!manager || *reinterpret_cast<void **>(manager)!=game+0x1804828) return result;
        void *inventory=*reinterpret_cast<void **>(manager+0x40);
        if (!inventory) return result;
        using Cast = void *(__cdecl *)(void *, long, void *, void *, int);
        auto cast=*reinterpret_cast<Cast *>(game+0x13cc1e8);
        if (!cast) return result;
        void *wallet=cast(inventory,0,game+0x1cdc800,game+0x1cdc688,0);
        if (!wallet || *reinterpret_cast<void **>(wallet)!=game+0x1804e08) return result;
        int amount=*reinterpret_cast<int *>(static_cast<unsigned char *>(wallet)+0x40);
        if (amount<0 || host!=*reinterpret_cast<void **>(game+0x1d07f88) ||
            get_player(host)!=player || get_level(actor+40)!=level ||
            *reinterpret_cast<void **>(actor+0x958)!=manager ||
            *reinterpret_cast<void **>(manager+0x40)!=inventory) return result;
        result={manager,wallet,amount};
    } __except(EXCEPTION_EXECUTE_HANDLER) { result={nullptr,nullptr,-1}; }
    return result;
}
void * __fastcall grant_hook(void *self, void *item, int reason, unsigned char a4, char a5, int a6, void *a7) {
    void *caller=_ReturnAddress();
    AcquireSRWLockExclusive(&lock);
    DoomLootAdapter *p=current.load(std::memory_order_acquire);
    int native_type=0;
    int kind=p ? classify(p,self,item,reason,caller,&native_type) : 0;
    uint64_t epoch=0;
    uint32_t generation=0;
    unsigned char *game=nullptr;
    void *player=nullptr, *level=nullptr;
    void *(__fastcall *get_level)(void *)=nullptr;
    if (kind && p->count+p->reserved<64) {
        ++p->reserved; epoch=p->epoch; generation=p->generation;
        game=p->game; player=p->player; level=p->level; get_level=p->get_level;
    } else kind=0;
    ReleaseSRWLockExclusive(&lock);
    int before=kind && native_type==4 ? ammo_count(game,player,item) : -1;
    CashSnapshot cash_before{nullptr,nullptr,-1};
    if (kind && native_type==1) cash_before=cash_count(game,player,level,get_level);
    void *result=nullptr;
    __try {
        result=original(self,item,reason,a4,a5,a6,a7);
    } __finally {
        const bool completed=!AbnormalTermination();
        bool same_context=false;
        if (kind) {
            AcquireSRWLockExclusive(&lock);
            same_context=current.load(std::memory_order_acquire)==p && p->enabled &&
                p->epoch==epoch && p->generation==generation && p->player==player && p->level==level;
            ReleaseSRWLockExclusive(&lock);
        }
        int after=completed && same_context && native_type==4 && before>=0 ? ammo_count(game,player,item) : -1;
        CashSnapshot cash_after{nullptr,nullptr,-1};
        if (completed && same_context && native_type==1 && cash_before.count>=0)
            cash_after=cash_count(game,player,level,get_level);
        bool cash_increased=cash_before.count>=0 && cash_after.manager==cash_before.manager &&
            cash_after.wallet==cash_before.wallet && cash_after.count>cash_before.count;
        if (kind) {
            AcquireSRWLockExclusive(&lock);
            if (current.load(std::memory_order_acquire)==p && p->epoch==epoch) {
                if (p->reserved) --p->reserved;
                if (completed && p->enabled && p->generation==generation && p->count<64 &&
                    (result || (before>=0 && after>before) || cash_increased))
                    p->pending[p->count++]={generation,static_cast<DoomLootKind>(kind)};
            }
            ReleaseSRWLockExclusive(&lock);
        }
    }
    return result;
}
unsigned char __fastcall trigger_hook(void *owner, unsigned long long action, void *context) {
    void *previous=container_item;
    container_item=owner;
    unsigned char result=0;
    __try { result=original_trigger(owner,action,context); }
    __finally { container_item=previous; }
    return result;
}
}
extern "C" DoomLootAdapter *doom_loot_adapter_create(void *module) {
    auto *game=static_cast<unsigned char *>(module);
    HMODULE engine=GetModuleHandleW(L"engine_x64_rwdi.dll");
    auto get_level=engine ? reinterpret_cast<void *(__fastcall *)(void *)>(GetProcAddress(engine,
        "?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ")) : nullptr;
    static const unsigned char cash_read[]={0x48,0x8b,0x80,0x58,0x09,0,0,0x4c,0x8d,0x0d};
    static const unsigned char ammo_get[]={0x40,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x48,0x85,0xc9};
    static const unsigned char grant[]={0x40,0x56,0x57,0x41,0x55,0x41,0x56,0x48,0x81,0xec,0xe8,0,0,0};
    static const unsigned char caller[]={0xff,0x50,0x60,0x48,0x85,0xc0,0x74,0x65};
    static const unsigned char type[]={0x48,0x8b,0x49,0x08,0x48,0x8b,0x01,0x48,0xff,0xa0,0xc0,0x01,0,0};
    static const unsigned char get_player[]={0x48,0x8b,0x81,0x08,0x05,0,0,0x48,0x85,0xc0,0x74,0x18,0x48,0x8b,0x80,0xb8};
    static const unsigned char trigger[]={0x40,0x56,0x57,0x48,0x81,0xec,0x38,0x01,0,0,0x80,0x79,0x50,0};
    static const unsigned char ground_calls[]={0x41,0xff,0x53,0x60};
    static const unsigned char container_calls[]={0x41,0xff,0x53,0x60,0x48,0x85,0xc0,0x75,0x10};
    if (!game || !get_level || GetModuleHandleW(L"gamedll_x64_rwdi.dll")!=module ||
        std::memcmp(game+0xb44070,grant,sizeof(grant)) || std::memcmp(game+0xc5e980,ammo_get,sizeof(ammo_get)) ||
        std::memcmp(game+0x4ef36b,cash_read,sizeof(cash_read)) ||
        !GetModuleHandleW(L"MSVCR100.dll") ||
        *reinterpret_cast<void **>(game+0x13cc1e8)!=reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"MSVCR100.dll"),"__RTDynamicCast")) || std::memcmp(game+0x9cb80b,caller,sizeof(caller)) ||
        std::memcmp(game+0x6c9840,type,sizeof(type)) || std::memcmp(game+0x3c2d90,get_player,sizeof(get_player)) ||
        std::memcmp(game+0x9daf40,trigger,sizeof(trigger)) ||
        std::memcmp(game+0x9db032,ground_calls,sizeof(ground_calls)) ||
        std::memcmp(game+0x9e959a,container_calls,sizeof(container_calls))) return nullptr;
    AcquireSRWLockExclusive(&lock);
    if (claimed || (retained && retained->game!=game)) { ReleaseSRWLockExclusive(&lock); return nullptr; }
    auto *p=retained;
    if (!p) { p=new (std::nothrow) DoomLootAdapter{}; if (p) { p->game=game; p->get_level=get_level; retained=p; } }
    if (p) claimed=true;
    ReleaseSRWLockExclusive(&lock);
    return p;
}
extern "C" int doom_loot_adapter_start(DoomLootAdapter *p) {
    if (!p) return 0;
    AcquireSRWLockExclusive(&lock);
    if (p->enabled) { ReleaseSRWLockExclusive(&lock); return 1; }
    if (current.load() && current.load()!=p) { ReleaseSRWLockExclusive(&lock); return 0; }
    MH_STATUS s=MH_Initialize();
    if (s!=MH_OK && s!=MH_ERROR_ALREADY_INITIALIZED) { ReleaseSRWLockExclusive(&lock); return 0; }
    if (!p->created) {
        s=MH_CreateHook(p->game+0xb44070,reinterpret_cast<void *>(&grant_hook),reinterpret_cast<void **>(&original));
        if (s!=MH_OK) { ReleaseSRWLockExclusive(&lock); return 0; }
        p->created=true;
    }
    if (!p->trigger_created) {
        s=MH_CreateHook(p->game+0x9daf40,reinterpret_cast<void *>(&trigger_hook),reinterpret_cast<void **>(&original_trigger));
        if (s!=MH_OK) { ReleaseSRWLockExclusive(&lock); return 0; }
        p->trigger_created=true;
    }
    HMODULE self=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&grant_hook),&self)) { ReleaseSRWLockExclusive(&lock); return 0; }
    p->published=true; current.store(p,std::memory_order_release);
    s=MH_QueueEnableHook(p->game+0x9daf40);
    if (s==MH_OK) s=MH_QueueEnableHook(p->game+0xb44070);
    if (s==MH_OK) s=MH_ApplyQueued();
    if (s!=MH_OK) { current.store(nullptr); ReleaseSRWLockExclusive(&lock); return 0; }
    p->enabled=true;
    ReleaseSRWLockExclusive(&lock);
    return 1;
}
extern "C" void doom_loot_adapter_context(DoomLootAdapter *p, void *player, void *level, uint32_t generation, int accepting) {
    if (!p) return;
    AcquireSRWLockExclusive(&lock);
    /* Accepted rewards belong to the Doom map inventory. An appearance refresh
       or temporary missing player must not cancel a native pickup in flight. */
    if (p->generation!=generation) {
        p->count=0; p->reserved=0; ++p->epoch; ++p->head_revision;
    }
    p->player=player; p->level=level; p->generation=generation; p->accepting=accepting!=0;
    ReleaseSRWLockExclusive(&lock);
}
extern "C" int doom_loot_adapter_peek(DoomLootAdapter *p, DoomLootRecord *out, uint64_t *token) {
    if (!p || !out || !token || !TryAcquireSRWLockExclusive(&lock)) return 0;
    const bool found=p->count!=0;
    if (found) { *out=p->pending[0]; *token=p->head_revision; }
    ReleaseSRWLockExclusive(&lock);
    return found ? 1 : 0;
}
extern "C" int doom_loot_adapter_consume(DoomLootAdapter *p, uint64_t token) {
    if (!p || !TryAcquireSRWLockExclusive(&lock)) return 0;
    const bool matched=p->count && token==p->head_revision;
    if (matched) {
        for (unsigned i=1;i<p->count;++i) p->pending[i-1]=p->pending[i];
        --p->count; ++p->head_revision;
    }
    ReleaseSRWLockExclusive(&lock);
    return matched ? 1 : 0;
}
extern "C" int doom_loot_adapter_rotate(DoomLootAdapter *p, uint64_t token) {
    if (!p || !TryAcquireSRWLockExclusive(&lock)) return 0;
    const bool matched=p->count>1 && token==p->head_revision;
    if (matched) {
        const DoomLootRecord head=p->pending[0];
        for (unsigned i=1;i<p->count;++i) p->pending[i-1]=p->pending[i];
        p->pending[p->count-1]=head;
        ++p->head_revision;
    }
    ReleaseSRWLockExclusive(&lock);
    return matched ? 1 : 0;
}
extern "C" int doom_loot_adapter_stop(DoomLootAdapter *p) {
    if (!p) return 1;
    AcquireSRWLockExclusive(&lock);
    p->enabled=false; p->accepting=false; p->player=nullptr; p->level=nullptr; p->count=0; p->reserved=0; ++p->epoch; ++p->head_revision;
    ReleaseSRWLockExclusive(&lock);
    if (p->created) {
        MH_STATUS s=MH_DisableHook(p->game+0xb44070);
        if (s!=MH_OK && s!=MH_ERROR_DISABLED) return 0;
    }
    if (p->trigger_created) {
        MH_STATUS s=MH_DisableHook(p->game+0x9daf40);
        if (s!=MH_OK && s!=MH_ERROR_DISABLED) return 0;
    }
    AcquireSRWLockExclusive(&lock);
    DoomLootAdapter *expected=p; current.compare_exchange_strong(expected,nullptr);
    ReleaseSRWLockExclusive(&lock);
    return 1;
}
extern "C" void doom_loot_adapter_destroy(DoomLootAdapter *p) {
    if (!p || !doom_loot_adapter_stop(p)) return;
    AcquireSRWLockExclusive(&lock);
    claimed=false;
    if (!p->published) {
        if (p->created) (void)MH_RemoveHook(p->game+0xb44070);
        if (p->trigger_created) (void)MH_RemoveHook(p->game+0x9daf40);
        retained=nullptr; delete p;
    }
    ReleaseSRWLockExclusive(&lock);
}
#else
extern "C" DoomLootAdapter *doom_loot_adapter_create(void *) { return nullptr; }
extern "C" int doom_loot_adapter_start(DoomLootAdapter *) { return 0; }
extern "C" void doom_loot_adapter_context(DoomLootAdapter *,void *,void *,uint32_t,int) {}
extern "C" int doom_loot_adapter_peek(DoomLootAdapter *,DoomLootRecord *,uint64_t *) { return 0; }
extern "C" int doom_loot_adapter_consume(DoomLootAdapter *,uint64_t) { return 0; }
extern "C" int doom_loot_adapter_rotate(DoomLootAdapter *,uint64_t) { return 0; }
extern "C" int doom_loot_adapter_stop(DoomLootAdapter *) { return 1; }
extern "C" void doom_loot_adapter_destroy(DoomLootAdapter *) {}
#endif
