/* Temporary map presentation. GPL-2.0-or-later. */
#include "doom_powerups_map.h"
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

struct DoomPowerupsMap {
    unsigned char *game;
    void **visible_slot;
    void *visible_original;
    std::atomic<bool> active{false};
    bool created=false, enabled=false, published=false, iat_owned=false;
};
namespace {
using SetVisible=void (__fastcall *)(void *, unsigned char);
using SetFogData=unsigned char (__fastcall *)(void *, const void *);
std::atomic<DoomPowerupsMap *> current{nullptr};
SetVisible visible_original=nullptr;
SetFogData fog_original=nullptr;
DoomPowerupsMap *retained=nullptr;
SRWLOCK guard=SRWLOCK_INIT;
bool claimed=false;
const unsigned char clear_fog[0x4000]{};

void __fastcall visible_hook(void *element, unsigned char visible) {
    DoomPowerupsMap *p=current.load(std::memory_order_acquire);
    void *caller=_ReturnAddress();
    if (p && p->active.load(std::memory_order_acquire) &&
        (caller==p->game+0x86b931 || caller==p->game+0x871cf2)) visible=0;
    visible_original(element,visible);
}
unsigned char __fastcall fog_hook(void *image, const void *data) {
    DoomPowerupsMap *p=current.load(std::memory_order_acquire);
    void *caller=_ReturnAddress();
    if (p && p->active.load(std::memory_order_acquire) &&
        (caller==p->game+0x86bd2e || caller==p->game+0x8720f8)) data=clear_fog;
    return fog_original(image,data);
}
bool swap_slot(void **slot,void *expected,void *replacement) {
    DWORD old=0;
    if (!VirtualProtect(slot,sizeof(void *),PAGE_READWRITE,&old)) return false;
    void *seen=InterlockedCompareExchangePointer(slot,replacement,expected);
    DWORD ignored=0;
    const BOOL restored=VirtualProtect(slot,sizeof(void *),old,&ignored);
    return seen==expected && restored!=0;
}
}
extern "C" DoomPowerupsMap *doom_powerups_map_create(void *module,void *engine_module) {
    auto *game=static_cast<unsigned char *>(module);
    auto engine=static_cast<HMODULE>(engine_module);
    static const unsigned char fog[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x8b};
    static const unsigned char tile_a[]={0xff,0x15,0xdf,0x2b,0xb6,0x00};
    static const unsigned char tile_b[]={0xff,0x15,0x1e,0xc8,0xb5,0x00};
    static const unsigned char data_a[]={0xe8,0x42,0xb9,0x00,0x00};
    static const unsigned char data_b[]={0xe8,0x78,0x55,0x00,0x00};
    if (!game || !engine || GetModuleHandleW(L"gamedll_x64_rwdi.dll")!=module ||
        GetModuleHandleW(L"engine_x64_rwdi.dll")!=engine_module ||
        std::memcmp(game+0x877670,fog,sizeof(fog)) || std::memcmp(game+0x86b92b,tile_a,sizeof(tile_a)) ||
        std::memcmp(game+0x871cec,tile_b,sizeof(tile_b)) || std::memcmp(game+0x86bd29,data_a,sizeof(data_a)) ||
        std::memcmp(game+0x8720f3,data_b,sizeof(data_b))) return nullptr;
    void *visible=reinterpret_cast<void *>(GetProcAddress(engine,"?SetVisible@IUIElement@@QEAAX_N@Z"));
    if (!visible || *reinterpret_cast<void **>(game+0x13ce510)!=visible) return nullptr;
    AcquireSRWLockExclusive(&guard);
    if (claimed || (retained && retained->game!=game)) { ReleaseSRWLockExclusive(&guard); return nullptr; }
    auto *p=retained;
    if (!p) {
        p=new(std::nothrow) DoomPowerupsMap{};
        if (p) { p->game=game; p->visible_slot=reinterpret_cast<void **>(game+0x13ce510); p->visible_original=visible; retained=p; }
    }
    if (p) claimed=true;
    ReleaseSRWLockExclusive(&guard);
    return p;
}
extern "C" int doom_powerups_map_start(DoomPowerupsMap *p) {
    if (!p) return 0;
    AcquireSRWLockExclusive(&guard);
    if (p->enabled) { ReleaseSRWLockExclusive(&guard); return 1; }
    if (current.load(std::memory_order_acquire)) { ReleaseSRWLockExclusive(&guard); return 0; }
    MH_STATUS s=MH_Initialize();
    if (s!=MH_OK && s!=MH_ERROR_ALREADY_INITIALIZED) { ReleaseSRWLockExclusive(&guard); return 0; }
    if (!p->created) {
        s=MH_CreateHook(p->game+0x877670,reinterpret_cast<void *>(&fog_hook),reinterpret_cast<void **>(&fog_original));
        if (s!=MH_OK) { ReleaseSRWLockExclusive(&guard); return 0; }
        p->created=true;
    }
    HMODULE self=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&visible_hook),&self)) { ReleaseSRWLockExclusive(&guard); return 0; }
    visible_original=reinterpret_cast<SetVisible>(p->visible_original);
    p->published=true; current.store(p,std::memory_order_release);
    s=MH_EnableHook(p->game+0x877670);
    if (s!=MH_OK && s!=MH_ERROR_ENABLED) { current.store(nullptr); ReleaseSRWLockExclusive(&guard); return 0; }
    if (!swap_slot(p->visible_slot,p->visible_original,reinterpret_cast<void *>(&visible_hook))) {
        p->iat_owned=*p->visible_slot==reinterpret_cast<void *>(&visible_hook);
        ReleaseSRWLockExclusive(&guard);
        (void)MH_DisableHook(p->game+0x877670);
        if (!p->iat_owned) current.store(nullptr,std::memory_order_release);
        return 0;
    }
    p->iat_owned=true; p->enabled=true;
    ReleaseSRWLockExclusive(&guard);
    return 1;
}
extern "C" void doom_powerups_map_set_active(DoomPowerupsMap *p,int active) {
    if (p) p->active.store(active!=0,std::memory_order_release);
}
extern "C" int doom_powerups_map_stop(DoomPowerupsMap *p) {
    if (!p) return 1;
    p->active.store(false,std::memory_order_release);
    if (p->iat_owned) {
        if (!swap_slot(p->visible_slot,reinterpret_cast<void *>(&visible_hook),p->visible_original)) return 0;
        p->iat_owned=false;
    }
    if (p->created) {
        MH_STATUS s=MH_DisableHook(p->game+0x877670);
        if (s!=MH_OK && s!=MH_ERROR_DISABLED) return 0;
    }
    AcquireSRWLockExclusive(&guard);
    p->enabled=false;
    DoomPowerupsMap *expected=p; current.compare_exchange_strong(expected,nullptr);
    ReleaseSRWLockExclusive(&guard);
    return 1;
}
extern "C" void doom_powerups_map_destroy(DoomPowerupsMap *p) {
    if (!p || !doom_powerups_map_stop(p)) return;
    AcquireSRWLockExclusive(&guard);
    claimed=false;
    if (!p->published) { if (p->created) (void)MH_RemoveHook(p->game+0x877670); retained=nullptr; delete p; }
    ReleaseSRWLockExclusive(&guard);
}
#else
extern "C" DoomPowerupsMap *doom_powerups_map_create(void *,void *) { return nullptr; }
extern "C" int doom_powerups_map_start(DoomPowerupsMap *) { return 0; }
extern "C" void doom_powerups_map_set_active(DoomPowerupsMap *,int) {}
extern "C" int doom_powerups_map_stop(DoomPowerupsMap *) { return 1; }
extern "C" void doom_powerups_map_destroy(DoomPowerupsMap *) {}
#endif
