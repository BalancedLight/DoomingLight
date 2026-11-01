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
    uint64_t epoch;
    DoomLootRecord pending[64];
    unsigned count, reserved;
    bool enabled, accepting, created, predicate_created, trigger_created, container_created, hud_created, published;
};
namespace {
using Grant = void *(__fastcall *)(void *, void *, int, unsigned char, char, int, void *);
using ItemType = int (__fastcall *)(void *);
using GetPlayer = void *(__fastcall *)(void *);
using Predicate = unsigned char (__fastcall *)(void *);
using Trigger = unsigned char (__fastcall *)(void *, unsigned long long, void *);
using HudCreate = void *(__fastcall *)(void *, const void *, void *);
using HudDelete = void *(__fastcall *)(void *, char);
std::atomic<DoomLootAdapter *> current{nullptr};
SRWLOCK lock = SRWLOCK_INIT;
Grant original = nullptr;
Predicate original_predicate = nullptr;
Trigger original_trigger = nullptr;
Trigger original_container = nullptr;
HudCreate original_hud = nullptr;
struct Transaction { void *item; void *predicate_caller; uint64_t epoch; int kind; };
thread_local Transaction transaction{};
thread_local void *container_item = nullptr;
thread_local const void *suppress_hud_item = nullptr;
DoomLootAdapter *retained = nullptr;
bool claimed = false;
void cancel_transaction_locked(DoomLootAdapter *p) {
    if (p && transaction.item && transaction.epoch==p->epoch && p->reserved) --p->reserved;
    transaction={};
}
void cancel_transaction() {
    AcquireSRWLockExclusive(&lock);
    cancel_transaction_locked(current.load(std::memory_order_acquire));
    ReleaseSRWLockExclusive(&lock);
}

/* Only unambiguously weapon, ammunition and medkit types are converted.
   Materials, quest items, cash, collectibles and unknown types remain native. */
int category(int type) {
    switch (type) {
    case 2: return DOOM_LOOT_HEALTH;
    case 4: case 11: case 12: case 13: case 15: case 16: case 17:
    case 18: case 19: case 20: case 21: case 22: case 23: case 24:
    case 34: case 35: case 36: case 37: case 38: case 39: case 40:
    case 119: return DOOM_LOOT_AMMO;
    default: return 0;
    }
}
int classify(DoomLootAdapter *p, void *interface_ptr, void *item, int reason, void *caller) {
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
        return category(reinterpret_cast<ItemType>(p->game+0x6c9840)(desc));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void * __fastcall grant_hook(void *self, void *item, int reason, unsigned char a4, char a5, int a6, void *a7) {
    void *caller=_ReturnAddress();
    AcquireSRWLockExclusive(&lock);
    DoomLootAdapter *p=current.load(std::memory_order_acquire);
    cancel_transaction_locked(p);
    int kind=p ? classify(p,self,item,reason,caller) : 0;
    if (kind && p->count+p->reserved<64) {
        if (caller==p->game+0x9cb80e)
            p->pending[p->count++]={p->generation,static_cast<DoomLootKind>(kind)};
        else {
            ++p->reserved;
            void *predicate_caller=caller==p->game+0x9db036 ? p->game+0x9db04d : p->game+0x9e95ab;
            transaction={item,predicate_caller,p->epoch,kind};
        }
        ReleaseSRWLockExclusive(&lock);
        return nullptr;
    }
    ReleaseSRWLockExclusive(&lock);
    return original(self,item,reason,a4,a5,a6,a7);
}
unsigned char __fastcall predicate_hook(void *item) {
    void *caller=_ReturnAddress();
    AcquireSRWLockExclusive(&lock);
    DoomLootAdapter *p=current.load(std::memory_order_acquire);
    bool accept=p && p->enabled && transaction.item==item && transaction.predicate_caller==caller &&
        transaction.epoch==p->epoch && p->reserved && p->count<64;
    if (accept) {
        --p->reserved;
        p->pending[p->count++]={p->generation,static_cast<DoomLootKind>(transaction.kind)};
        if (caller==p->game+0x9e95ab) suppress_hud_item=item;
    }
    if (transaction.item==item && transaction.predicate_caller==caller) {
        if (accept) transaction={}; else cancel_transaction_locked(p);
    }
    ReleaseSRWLockExclusive(&lock);
    return accept ? 1 : original_predicate(item);
}
unsigned char __fastcall trigger_hook(void *owner, unsigned long long action, void *context) {
    void *previous=container_item;
    Transaction previous_transaction=transaction;
    container_item=owner;
    transaction={};
    unsigned char result=original_trigger(owner,action,context);
    cancel_transaction();
    container_item=previous;
    transaction=previous_transaction;
    return result;
}
unsigned char __fastcall container_hook(void *owner, unsigned long long action, void *context) {
    Transaction previous_transaction=transaction;
    const void *previous_hud=suppress_hud_item;
    transaction={}; suppress_hud_item=nullptr;
    unsigned char result=original_container(owner,action,context);
    cancel_transaction();
    transaction=previous_transaction; suppress_hud_item=previous_hud;
    return result;
}
void * __fastcall hud_hook(void *storage, const void *item, void *owner) {
    void *caller=_ReturnAddress();
    void *event=original_hud(storage,item,owner);
    DoomLootAdapter *p=current.load(std::memory_order_acquire);
    if (p && event && caller==p->game+0x9e95ef && item==suppress_hud_item &&
        *reinterpret_cast<void **>(event)==p->game+0x1547978) {
        suppress_hud_item=nullptr;
        reinterpret_cast<HudDelete>(p->game+0x272670)(event,1);
        return nullptr; /* The native queue accepts a null event. */
    }
    return event;
}
}
extern "C" DoomLootAdapter *doom_loot_adapter_create(void *module) {
    auto *game=static_cast<unsigned char *>(module);
    HMODULE engine=GetModuleHandleW(L"engine_x64_rwdi.dll");
    auto get_level=engine ? reinterpret_cast<void *(__fastcall *)(void *)>(GetProcAddress(engine,
        "?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ")) : nullptr;
    static const unsigned char grant[]={0x40,0x56,0x57,0x41,0x55,0x41,0x56,0x48,0x81,0xec,0xe8,0,0,0};
    static const unsigned char caller[]={0xff,0x50,0x60,0x48,0x85,0xc0,0x74,0x65};
    static const unsigned char type[]={0x48,0x8b,0x49,0x08,0x48,0x8b,0x01,0x48,0xff,0xa0,0xc0,0x01,0,0};
    static const unsigned char get_player[]={0x48,0x8b,0x81,0x08,0x05,0,0,0x48,0x85,0xc0,0x74,0x18,0x48,0x8b,0x80,0xb8};
    static const unsigned char predicate[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x41,0x18,0x48,0x8d,0x59,0x18};
    static const unsigned char trigger[]={0x40,0x56,0x57,0x48,0x81,0xec,0x38,0x01,0,0,0x80,0x79,0x50,0};
    static const unsigned char ground_calls[]={0x41,0xff,0x53,0x60};
    static const unsigned char container_calls[]={0x41,0xff,0x53,0x60,0x48,0x85,0xc0,0x75,0x10};
    static const unsigned char container[]={0x40,0x53,0x55,0x48,0x81,0xec,0x88,0,0,0};
    static const unsigned char hud[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0xc7,0x41,0x08,0x05,0,0};
    static const unsigned char hud_delete[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0xda,0x48,0x8b,0xf9,0xe8};
    static const unsigned char hud_call[]={0xe8,0x21,0x90,0x88,0xff,0x48,0x8b,0xe8};
    if (!game || !get_level || GetModuleHandleW(L"gamedll_x64_rwdi.dll")!=module ||
        std::memcmp(game+0xb44070,grant,sizeof(grant)) || std::memcmp(game+0x9cb80b,caller,sizeof(caller)) ||
        std::memcmp(game+0x6c9840,type,sizeof(type)) || std::memcmp(game+0x3c2d90,get_player,sizeof(get_player)) ||
        std::memcmp(game+0x6997e0,predicate,sizeof(predicate)) || std::memcmp(game+0x9daf40,trigger,sizeof(trigger)) ||
        std::memcmp(game+0x9db032,ground_calls,sizeof(ground_calls)) ||
        std::memcmp(game+0x9e959a,container_calls,sizeof(container_calls)) ||
        std::memcmp(game+0x9e9260,container,sizeof(container)) || std::memcmp(game+0x272610,hud,sizeof(hud)) ||
        std::memcmp(game+0x272670,hud_delete,sizeof(hud_delete)) || std::memcmp(game+0x9e95ea,hud_call,sizeof(hud_call)) ||
        *reinterpret_cast<void **>(game+0x1547978)!=game+0x272670) return nullptr;
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
    if (!p->predicate_created) {
        s=MH_CreateHook(p->game+0x6997e0,reinterpret_cast<void *>(&predicate_hook),reinterpret_cast<void **>(&original_predicate));
        if (s!=MH_OK) { ReleaseSRWLockExclusive(&lock); return 0; }
        p->predicate_created=true;
    }
    if (!p->trigger_created) {
        s=MH_CreateHook(p->game+0x9daf40,reinterpret_cast<void *>(&trigger_hook),reinterpret_cast<void **>(&original_trigger));
        if (s!=MH_OK) { ReleaseSRWLockExclusive(&lock); return 0; }
        p->trigger_created=true;
    }
    if (!p->container_created) {
        s=MH_CreateHook(p->game+0x9e9260,reinterpret_cast<void *>(&container_hook),reinterpret_cast<void **>(&original_container));
        if (s!=MH_OK) { ReleaseSRWLockExclusive(&lock); return 0; }
        p->container_created=true;
    }
    if (!p->hud_created) {
        s=MH_CreateHook(p->game+0x272610,reinterpret_cast<void *>(&hud_hook),reinterpret_cast<void **>(&original_hud));
        if (s!=MH_OK) { ReleaseSRWLockExclusive(&lock); return 0; }
        p->hud_created=true;
    }
    HMODULE self=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&grant_hook),&self)) { ReleaseSRWLockExclusive(&lock); return 0; }
    p->published=true; current.store(p,std::memory_order_release);
    s=MH_QueueEnableHook(p->game+0x6997e0);
    if (s==MH_OK) s=MH_QueueEnableHook(p->game+0x9daf40);
    if (s==MH_OK) s=MH_QueueEnableHook(p->game+0x9e9260);
    if (s==MH_OK) s=MH_QueueEnableHook(p->game+0x272610);
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
        p->count=0; p->reserved=0; ++p->epoch;
    }
    p->player=player; p->level=level; p->generation=generation; p->accepting=accepting!=0;
    ReleaseSRWLockExclusive(&lock);
}
extern "C" unsigned doom_loot_adapter_drain(DoomLootAdapter *p, DoomLootRecord *out, unsigned capacity) {
    if (!p || !out || !capacity || !TryAcquireSRWLockExclusive(&lock)) return 0;
    unsigned n=p->count<capacity ? p->count : capacity;
    for (unsigned i=0;i<n;++i) out[i]=p->pending[i];
    for (unsigned i=n;i<p->count;++i) p->pending[i-n]=p->pending[i];
    p->count-=n;
    ReleaseSRWLockExclusive(&lock);
    return n;
}
extern "C" int doom_loot_adapter_stop(DoomLootAdapter *p) {
    if (!p) return 1;
    AcquireSRWLockExclusive(&lock);
    p->enabled=false; p->accepting=false; p->player=nullptr; p->level=nullptr; p->count=0; p->reserved=0; ++p->epoch;
    ReleaseSRWLockExclusive(&lock);
    if (p->created) {
        MH_STATUS s=MH_DisableHook(p->game+0xb44070);
        if (s!=MH_OK && s!=MH_ERROR_DISABLED) return 0;
    }
    if (p->predicate_created) {
        MH_STATUS s=MH_DisableHook(p->game+0x6997e0);
        if (s!=MH_OK && s!=MH_ERROR_DISABLED) return 0;
    }
    if (p->trigger_created) {
        MH_STATUS s=MH_DisableHook(p->game+0x9daf40);
        if (s!=MH_OK && s!=MH_ERROR_DISABLED) return 0;
    }
    if (p->container_created) {
        MH_STATUS s=MH_DisableHook(p->game+0x9e9260);
        if (s!=MH_OK && s!=MH_ERROR_DISABLED) return 0;
    }
    if (p->hud_created) {
        MH_STATUS s=MH_DisableHook(p->game+0x272610);
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
        if (p->predicate_created) (void)MH_RemoveHook(p->game+0x6997e0);
        if (p->trigger_created) (void)MH_RemoveHook(p->game+0x9daf40);
        if (p->container_created) (void)MH_RemoveHook(p->game+0x9e9260);
        if (p->hud_created) (void)MH_RemoveHook(p->game+0x272610);
        retained=nullptr; delete p;
    }
    ReleaseSRWLockExclusive(&lock);
}
#else
extern "C" DoomLootAdapter *doom_loot_adapter_create(void *) { return nullptr; }
extern "C" int doom_loot_adapter_start(DoomLootAdapter *) { return 0; }
extern "C" void doom_loot_adapter_context(DoomLootAdapter *,void *,void *,uint32_t,int) {}
extern "C" unsigned doom_loot_adapter_drain(DoomLootAdapter *,DoomLootRecord *,unsigned) { return 0; }
extern "C" int doom_loot_adapter_stop(DoomLootAdapter *) { return 1; }
extern "C" void doom_loot_adapter_destroy(DoomLootAdapter *) {}
#endif
