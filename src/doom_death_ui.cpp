#include "doom_death_ui.h"

#if defined(DOOM_MODE_RUNTIME)
#include <windows.h>
#include <atomic>
#include <cstring>

namespace {
using Update = void (__fastcall *)(void *);
SRWLOCK hook_lock=SRWLOCK_INIT;
thread_local unsigned hook_depth=0;
std::atomic<bool> enabled{false};
Update original=nullptr;
void **slot=nullptr;
DoomDeathUiAfterUpdate after_update=nullptr;
void *callback_context=nullptr;

void __fastcall update_hook(void *object) {
    if (!hook_depth) AcquireSRWLockShared(&hook_lock);
    ++hook_depth;
    __try {
        if (original) original(object);
        if (hook_depth==1 && enabled.load(std::memory_order_acquire) && after_update)
            after_update(callback_context);
    } __finally {
        if (!--hook_depth) ReleaseSRWLockShared(&hook_lock);
    }
}
}

extern "C" int doom_death_ui_start(void *game_module,DoomDeathUiAfterUpdate callback,void *context) {
    auto *game=static_cast<unsigned char *>(game_module);
    static const unsigned char prefix[]={0x48,0x89,0x4c,0x24,0x08,0x55,0x53,0x56,
                                         0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c};
    if (!game || !callback || !context || slot ||
        GetModuleHandleW(L"gamedll_x64_rwdi.dll")!=game_module) return 0;
    __try {
        if (std::memcmp(game+0x4dbcb0,prefix,sizeof(prefix))) return 0;
        auto **target=reinterpret_cast<void **>(game+0x15c04d8);
        if (*target!=game+0x4dbcb0) return 0;
        HMODULE pinned=nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCWSTR>(&update_hook),&pinned)) return 0;
        original=reinterpret_cast<Update>(game+0x4dbcb0);
        after_update=callback; callback_context=context;
        DWORD previous=0;
        if (!VirtualProtect(target,sizeof(void *),PAGE_READWRITE,&previous)) return 0;
        void *seen=InterlockedCompareExchangePointer(target,reinterpret_cast<void *>(&update_hook),
                                                     reinterpret_cast<void *>(original));
        if (seen==reinterpret_cast<void *>(original)) slot=target;
        DWORD ignored=0;
        const bool restored=VirtualProtect(target,sizeof(void *),previous,&ignored)!=0;
        if (seen!=reinterpret_cast<void *>(original)) return 0;
        enabled.store(true,std::memory_order_release);
        if (!restored) return doom_death_ui_stop() ? 0 : -1;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

extern "C" int doom_death_ui_stop(void) {
    enabled.store(false,std::memory_order_release);
    if (!slot) return 1;
    DWORD previous=0;
    if (!VirtualProtect(slot,sizeof(void *),PAGE_READWRITE,&previous)) return 0;
    void *seen=InterlockedCompareExchangePointer(slot,reinterpret_cast<void *>(original),
                                                 reinterpret_cast<void *>(&update_hook));
    DWORD ignored=0;
    const bool restored=VirtualProtect(slot,sizeof(void *),previous,&ignored)!=0;
    if ((seen!=reinterpret_cast<void *>(&update_hook) && seen!=reinterpret_cast<void *>(original)) || !restored)
        return 0;
    /* The callback can be waiting on the engine lock held by the caller. */
    if (hook_depth || !TryAcquireSRWLockExclusive(&hook_lock)) return 0;
    slot=nullptr; after_update=nullptr; callback_context=nullptr;
    ReleaseSRWLockExclusive(&hook_lock);
    return 1;
}
#else
extern "C" int doom_death_ui_start(void *,DoomDeathUiAfterUpdate,void *) { return 0; }
extern "C" int doom_death_ui_stop(void) { return 1; }
#endif
