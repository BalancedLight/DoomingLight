#include "doom_player_voice.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>

#if defined(DOOM_MODE_LAB) && defined(_DEBUG) && !defined(NDEBUG)
namespace {
constexpr uintptr_t GameGlobal = 0x1D07F88, GameVtable = 0x157D5F8;
constexpr uintptr_t PlayerVtable = 0x17DD738, GetLocalPlayer = 0x3C2D90;
constexpr uintptr_t PlayerSound = 0xBE6660;
constexpr uintptr_t Tables[] = {0x17ED148, 0x17E1188, 0x17E3D38};
constexpr unsigned SoundSlot = 364;
using PlaySound = unsigned (__fastcall *)(void *, unsigned, unsigned, unsigned char,
                                            float, float, float, int, float);
using GetPlayer = void *(__fastcall *)(void *);
std::atomic<DoomPlayerVoice *> current{nullptr};
PlaySound original = nullptr;
template<class T> T field(void *p, size_t offset) {
    return *reinterpret_cast<T *>(static_cast<unsigned char *>(p) + offset);
}
}
struct DoomPlayerVoice {
    unsigned char *game = nullptr;
    std::atomic<bool> active{false};
    bool installed[3]{};
    bool published = false;
};
namespace {
bool is_local_reaction(DoomPlayerVoice *filter, void *view, uint32_t category) {
    if (!filter || !view || !((category >= 3 && category <= 6) || category == 9)) return false;
    __try {
        void *game = *reinterpret_cast<void **>(filter->game + GameGlobal);
        if (!game || *reinterpret_cast<void **>(game) != filter->game + GameVtable) return false;
        void *player = reinterpret_cast<GetPlayer>(filter->game + GetLocalPlayer)(game);
        return player && *reinterpret_cast<void **>(player) == filter->game + PlayerVtable &&
               field<void *>(view, 0x50) == player &&
               player == reinterpret_cast<GetPlayer>(filter->game + GetLocalPlayer)(game);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
unsigned __fastcall play_hook(void *view, unsigned id, unsigned flags, unsigned char mode,
                               float p5, float p6, float p7, int p8, float p9) {
    DoomPlayerVoice *filter = current.load(std::memory_order_acquire);
    if (filter && filter->active.load(std::memory_order_acquire) &&
        is_local_reaction(filter, view, id))
        return 0;
    return original(view, id, flags, mode, p5, p6, p7, p8, p9);
}
bool replace_slot(void **slot, void *expected, void *replacement, bool *changed) {
    DWORD protection = 0;
    *changed = false;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return false;
    void *seen = InterlockedCompareExchangePointer(slot, replacement, expected);
    *changed = seen == expected;
    DWORD ignored = 0;
    return VirtualProtect(slot, sizeof(*slot), protection, &ignored) != 0 && *changed;
}
}
extern "C" DoomPlayerVoice *doom_player_voice_create(void *module) {
    if (!module || module != GetModuleHandleW(L"gamedll_x64_rwdi.dll")) return nullptr;
    auto *filter = new (std::nothrow) DoomPlayerVoice;
    if (filter) filter->game = static_cast<unsigned char *>(module);
    return filter;
}
extern "C" int doom_player_voice_install(DoomPlayerVoice *filter) {
    if (!filter || !filter->game) return 0;
    static const unsigned char prefix[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10};
    static const unsigned char player_prefix[] = {0x48,0x8b,0x81,0x08,0x05,0,0,0x48,0x85,0xc0,0x74};
    __try {
        if (std::memcmp(filter->game + PlayerSound, prefix, sizeof prefix) ||
            std::memcmp(filter->game + GetLocalPlayer, player_prefix, sizeof player_prefix)) return 0;
        for (unsigned i = 0; i < 3; ++i) {
            if (filter->installed[i] || reinterpret_cast<void **>(filter->game + Tables[i])[SoundSlot] != filter->game + PlayerSound)
                return 0;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    DoomPlayerVoice *empty = nullptr;
    if (!current.compare_exchange_strong(empty, filter, std::memory_order_acq_rel)) return 0;
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&play_hook), &pinned)) {
        current.store(nullptr, std::memory_order_release); return 0;
    }
    filter->published = true;
    original = reinterpret_cast<PlaySound>(filter->game + PlayerSound);
    for (unsigned i = 0; i < 3; ++i) {
        bool changed = false;
        bool ok = replace_slot(reinterpret_cast<void **>(filter->game + Tables[i]) + SoundSlot,
                               filter->game + PlayerSound, reinterpret_cast<void *>(&play_hook), &changed);
        filter->installed[i] = changed;
        if (!ok) { (void)doom_player_voice_uninstall(filter); return 0; }
    }
    return 1;
}
extern "C" void doom_player_voice_set_active(DoomPlayerVoice *filter, int active) {
    if (filter) filter->active.store(active != 0, std::memory_order_release);
}
extern "C" int doom_player_voice_uninstall(DoomPlayerVoice *filter) {
    if (!filter) return 0;
    filter->active.store(false, std::memory_order_release);
    bool ok = true;
    for (unsigned i = 0; i < 3; ++i) if (filter->installed[i]) {
        bool changed = false;
        bool restored = replace_slot(reinterpret_cast<void **>(filter->game + Tables[i]) + SoundSlot,
                                     reinterpret_cast<void *>(&play_hook), filter->game + PlayerSound, &changed);
        if (changed) filter->installed[i] = false;
        if (!restored) ok = false;
    }
    if (ok) {
        DoomPlayerVoice *expected = filter;
        (void)current.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }
    return ok ? 1 : 0;
}
extern "C" void doom_player_voice_destroy(DoomPlayerVoice *filter) {
    if (!filter) return;
    (void)doom_player_voice_uninstall(filter);
    if (!filter->published) delete filter;
}
#else
struct DoomPlayerVoice {};
extern "C" DoomPlayerVoice *doom_player_voice_create(void *) { return nullptr; }
extern "C" int doom_player_voice_install(DoomPlayerVoice *) { return 0; }
extern "C" void doom_player_voice_set_active(DoomPlayerVoice *, int) {}
extern "C" int doom_player_voice_uninstall(DoomPlayerVoice *) { return 1; }
extern "C" void doom_player_voice_destroy(DoomPlayerVoice *) {}
#endif
