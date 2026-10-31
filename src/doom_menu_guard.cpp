#include "doom_menu_guard.h"
#include "doom_noclip.h"

#if defined(DOOM_MODE_RUNTIME)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace {
using ExecuteAction = uintptr_t (__fastcall *)(void *, void *, float, unsigned char);
using GetPlayer = void *(__fastcall *)(void *);

std::atomic<bool> enabled{false};
unsigned char *game = nullptr;
ExecuteAction original = nullptr;
bool created = false;

template<class T> T field(const void *base, size_t offset) {
    return *reinterpret_cast<const T *>(static_cast<const unsigned char *>(base)+offset);
}

bool suppress(void *action, float *neutral) {
    if (!enabled.load(std::memory_order_acquire) || !action || !neutral || !game) return false;
    __try {
        const unsigned id=field<unsigned>(action,0);
        if (!doom_menu_guard_blocks_action(id)) return false;
        void *owner=field<void *>(action,64);
        void *singleton=field<void *>(game,0x1d07f88);
        if (!singleton || field<void *>(singleton,0)!=game+0x157d5f8 ||
            !owner || field<void *>(singleton,200)!=owner)
            return false;
        const bool flying=doom_noclip_input_ready()!=0;
        if (field<int>(owner,308)!=0 && !flying) return false;
        void *session=field<void *>(singleton,1288);
        void *camera_manager=session ? field<void *>(session,192) : nullptr;
        DWORD pid=0;
        GetWindowThreadProcessId(GetForegroundWindow(),&pid);
        if (pid!=GetCurrentProcessId()) return false;
        const bool ordinary=camera_manager && field<void *>(camera_manager,64) &&
            (field<int>(camera_manager,88)==1 || field<int>(camera_manager,88)==2) &&
            reinterpret_cast<GetPlayer>(game+0x3c2d90)(singleton);
        if (!ordinary && !flying) return false;
        // InputAction applies this inversion before dispatching its listeners.
        *neutral=(field<unsigned char>(action,16)&0x10u) ? 1.0f : 0.0f;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uintptr_t __fastcall execute_hook(void *action, void *targets, float value, unsigned char flags) {
    float neutral=0.0f;
    if (suppress(action,&neutral)) value=neutral;
    return original(action,targets,value,flags);
}
}

extern "C" int doom_menu_guard_start(void *game_module, int supported_profile) {
    auto *base=static_cast<unsigned char *>(game_module);
    static const unsigned char prefix[]={0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x7c,
                                          0x24,0x20,0x41,0x54,0x48,0x83,0xec,0x30};
    if (!supported_profile || !base || GetModuleHandleW(L"gamedll_x64_rwdi.dll")!=game_module ||
        std::memcmp(base+0x114d780,prefix,sizeof(prefix)) || (game && game!=base)) return 0;
    HMODULE self=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&execute_hook),&self)) return 0;
    MH_STATUS status=MH_Initialize();
    if (status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED) return 0;
    if (!created) {
        status=MH_CreateHook(base+0x114d780,reinterpret_cast<void *>(&execute_hook),
                             reinterpret_cast<void **>(&original));
        if (status!=MH_OK) return 0;
        game=base; created=true;
    }
    enabled.store(true,std::memory_order_release);
    status=MH_EnableHook(base+0x114d780);
    if (status!=MH_OK && status!=MH_ERROR_ENABLED) {
        enabled.store(false,std::memory_order_release); return 0;
    }
    return 1;
}

extern "C" int doom_menu_guard_stop(void) {
    enabled.store(false,std::memory_order_release);
    if (!created || !game) return 1;
    const MH_STATUS status=MH_DisableHook(game+0x114d780);
    // Keep the trampoline alive for any already-entered callback.
    return status==MH_OK || status==MH_ERROR_DISABLED;
}
#else
extern "C" int doom_menu_guard_start(void *, int) { return 0; }
extern "C" int doom_menu_guard_stop(void) { return 1; }
#endif
