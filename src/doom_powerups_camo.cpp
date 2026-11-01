#include "doom_powerups_camo.h"
#include <windows.h>
#include <MinHook.h>
#include <cmath>
#include <cstring>

namespace {
using Start = uintptr_t (__fastcall *)(void *, float, unsigned char, unsigned char);
using GetPlayer = void *(__fastcall *)(void *);
SRWLOCK lock = SRWLOCK_INIT;
unsigned char *game_base = nullptr;
Start original = nullptr;
void *watched = nullptr;
unsigned external_epoch = 0, owned_epoch = 0;
bool created = false, enabled_hook = false, owned = false;
thread_local bool own_call = false;

uintptr_t __fastcall observe_start(void *module, float duration, unsigned char first, unsigned char second) {
    if (own_call) return original(module, duration, first, second);
    uintptr_t result = 0;
    AcquireSRWLockExclusive(&lock);
    __try {
        if (module == watched) ++external_epoch;
        own_call = true;
        result = original(module, duration, first, second);
    } __finally {
        own_call = false;
        ReleaseSRWLockExclusive(&lock);
    }
    return result;
}

void *local_module(void *player) {
    if (!game_base || !player) return nullptr;
    __try {
        void *game = *reinterpret_cast<void **>(game_base + 0x1d07f88);
        if (!game || *reinterpret_cast<void **>(game) != game_base + 0x157d5f8 ||
            reinterpret_cast<GetPlayer>(game_base + 0x3c2d90)(game) != player ||
            *reinterpret_cast<void **>(player) != game_base + 0x17dd738 ||
            *reinterpret_cast<void **>(static_cast<unsigned char *>(player) + 40) != game_base + 0x17de468)
            return nullptr;
        void *module = *reinterpret_cast<void **>(static_cast<unsigned char *>(player) + 0x13e8);
        return module && *reinterpret_cast<void **>(module) == game_base + 0x1805528 ? module : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
}

extern "C" int doom_powerups_camo_start(void *game_module) {
    auto *base = static_cast<unsigned char *>(game_module);
    static const unsigned char prefix[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x50};
    if (!base || GetModuleHandleW(L"gamedll_x64_rwdi.dll") != game_module) return 0;
    if (enabled_hook) return game_base == base;
    __try { if (std::memcmp(base + 0xc64970, prefix, sizeof(prefix))) return 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return 0;
    if (!created) {
        void *trampoline = nullptr;
        HMODULE self = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&observe_start), &self)) return 0;
        if (MH_CreateHook(base + 0xc64970, reinterpret_cast<void *>(&observe_start), &trampoline) != MH_OK) return 0;
        original = reinterpret_cast<Start>(trampoline); game_base = base; created = true;
    }
    if (game_base != base || MH_EnableHook(base + 0xc64970) != MH_OK) return 0;
    enabled_hook = true;
    return 1;
}

extern "C" int doom_powerups_camo_update(void *player, int requested, float seconds) {
    if (!enabled_hook || !std::isfinite(seconds) || seconds < 0.0f || seconds > 120.0f) return 0;
    void *module = local_module(player);
    if (!module) return 0;
    int result = 1;
    AcquireSRWLockExclusive(&lock);
    __try {
        if (local_module(player) != module) { result = 0; __leave; }
        if (watched != module) { watched = module; owned = false; }
        if (owned && owned_epoch != external_epoch) owned = false;
        float *left = reinterpret_cast<float *>(static_cast<unsigned char *>(module) + 20);
        if (owned && *left <= 0.0f) owned = false;
        if (!requested) {
            if (owned) *reinterpret_cast<unsigned long long *>(static_cast<unsigned char *>(module) + 16) = 0;
            owned = false;
        } else if (!owned && *left <= 0.0f && seconds > 0.0f) {
            own_call = true;
            original(module, seconds, 0, 0);
            own_call = false;
            owned = true; owned_epoch = external_epoch;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { own_call = false; owned = false; result = 0; }
    ReleaseSRWLockExclusive(&lock);
    return result;
}

extern "C" int doom_powerups_camo_stop(void *player) {
    if (!enabled_hook) return 1;
    if (!player && owned) return 0;
    if (player && !doom_powerups_camo_update(player, 0, 0.0f)) return 0;
    return doom_powerups_camo_forget();
}

extern "C" int doom_powerups_camo_forget(void) {
    if (!enabled_hook) return 1;
    if (MH_DisableHook(game_base + 0xc64970) != MH_OK) return 0;
    AcquireSRWLockExclusive(&lock);
    watched = nullptr; owned = false; enabled_hook = false;
    ReleaseSRWLockExclusive(&lock);
    return 1;
}
