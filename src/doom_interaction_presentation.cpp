#include "doom_interaction_presentation.h"

#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>

#if defined(DOOM_MODE_RUNTIME)
namespace {
constexpr uintptr_t InteractionControllerVtable = 0x1809C78;
constexpr uintptr_t InteractionLookVtable = 0x1809E08;
constexpr uintptr_t InteractionWantedLook = 0xCBE3A0;
constexpr uintptr_t GameGlobal = 0x1D07F88;
constexpr uintptr_t GetLocalPlayer = 0x3C2D90;
constexpr uintptr_t GameVtable = 0x157D5F8;
constexpr uintptr_t GameFrameVtable = 0x157E2E8;
constexpr uintptr_t PlayerVtable = 0x17DD738;
constexpr unsigned char WantedLookPrefix[] = {
    0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x40
};
constexpr unsigned char LocalPlayerPrefix[] = {
    0x48,0x8b,0x81,0x08,0x05,0x00,0x00,0x48,0x85,0xc0,0x74
};
using WantedLook = float *(__fastcall *)(void *, float *);
using GetPlayer = void *(__fastcall *)(void *);
std::atomic<DoomInteractionPresentation *> current{nullptr};
WantedLook original = nullptr;

template<class T> T field(void *p, size_t offset)
{
    return *reinterpret_cast<T *>(static_cast<unsigned char *>(p) + offset);
}
}

struct DoomInteractionPresentation {
    unsigned char *game = nullptr;
    void **slot = nullptr;
    std::atomic<bool> active{false};
    bool installed = false;
    bool published = false;
};

namespace {
bool wanted_look_override(DoomInteractionPresentation *p, void *look, float *out)
{
    __try {
        auto *controller = static_cast<unsigned char *>(look) - 0x20;
        if (*reinterpret_cast<void **>(look) != p->game + InteractionLookVtable ||
            *reinterpret_cast<void **>(controller) != p->game + InteractionControllerVtable)
            return false;
        void *game = *reinterpret_cast<void **>(p->game + GameGlobal);
        if (!game || *reinterpret_cast<void **>(game) != p->game + GameVtable ||
            field<void *>(game, 16) != p->game + GameFrameVtable)
            return false;
        void *player = reinterpret_cast<GetPlayer>(p->game + GetLocalPlayer)(game);
        if (!player || *reinterpret_cast<void **>(player) != p->game + PlayerVtable ||
            field<void *>(controller, 8) != player)
            return false;
        void *input = field<void *>(game, 200);
        void *session = field<void *>(game, 1288);
        void *manager = session ? field<void *>(session, 192) : nullptr;
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (pid != GetCurrentProcessId() || !input || field<int>(input, 308) != 0 ||
            !manager || !field<void *>(manager, 64) ||
            (field<int>(manager, 88) != 1 && field<int>(manager, 88) != 2))
            return false;
        const float yaw = field<float>(player, 4500);
        const float pitch = field<float>(player, 4504);
        if (!std::isfinite(yaw) || !std::isfinite(pitch)) return false;
        out[0] = yaw;
        out[1] = pitch;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

float *wanted_look_hook(void *look, float *out)
{
    DoomInteractionPresentation *p = current.load(std::memory_order_acquire);
    if (p && p->active.load(std::memory_order_acquire) && look && out &&
        wanted_look_override(p, look, out))
        return out;
    return original(look, out);
}
}

extern "C" DoomInteractionPresentation *doom_interaction_presentation_create(void *game_module)
{
    if (!game_module || game_module != GetModuleHandleW(L"gamedll_x64_rwdi.dll")) return nullptr;
    auto *p = new (std::nothrow) DoomInteractionPresentation;
    if (p) p->game = static_cast<unsigned char *>(game_module);
    return p;
}

extern "C" int doom_interaction_presentation_install(DoomInteractionPresentation *p)
{
    if (!p || !p->game || p->installed ||
        std::memcmp(p->game + InteractionWantedLook, WantedLookPrefix, sizeof(WantedLookPrefix)) ||
        std::memcmp(p->game + GetLocalPlayer, LocalPlayerPrefix, sizeof(LocalPlayerPrefix)))
        return 0;
    void **slot = reinterpret_cast<void **>(p->game + InteractionLookVtable);
    void *expected = p->game + InteractionWantedLook;
    if (*slot != expected) return 0;
    DoomInteractionPresentation *empty = nullptr;
    if (!current.compare_exchange_strong(empty, p, std::memory_order_acq_rel)) return 0;
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&wanted_look_hook), &pinned)) {
        current.store(nullptr, std::memory_order_release);
        return 0;
    }
    p->published = true;
    original = reinterpret_cast<WantedLook>(expected);
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) {
        current.store(nullptr, std::memory_order_release);
        return 0;
    }
    void *previous = InterlockedCompareExchangePointer(slot,
        reinterpret_cast<void *>(&wanted_look_hook), expected);
    DWORD ignored = 0;
    const bool restored = VirtualProtect(slot, sizeof(*slot), protection, &ignored) != 0;
    if (previous != expected) {
        current.store(nullptr, std::memory_order_release);
        return 0;
    }
    p->slot = slot;
    p->installed = true;
    if (!restored) {
        (void)doom_interaction_presentation_uninstall(p);
        (void)VirtualProtect(slot, sizeof(*slot), protection, &ignored);
        return 0;
    }
    return 1;
}

extern "C" void doom_interaction_presentation_set_active(DoomInteractionPresentation *p, int active)
{
    if (p) p->active.store(active != 0, std::memory_order_release);
}

extern "C" int doom_interaction_presentation_uninstall(DoomInteractionPresentation *p)
{
    if (!p) return 0;
    p->active.store(false, std::memory_order_release);
    if (!p->installed) return 1;
    DWORD protection = 0;
    if (!VirtualProtect(p->slot, sizeof(*p->slot), PAGE_READWRITE, &protection)) return 0;
    void *seen = InterlockedCompareExchangePointer(p->slot, reinterpret_cast<void *>(original),
        reinterpret_cast<void *>(&wanted_look_hook));
    DWORD ignored = 0;
    const bool restored = VirtualProtect(p->slot, sizeof(*p->slot), protection, &ignored) != 0;
    if ((seen != reinterpret_cast<void *>(&wanted_look_hook) && seen != reinterpret_cast<void *>(original)) || !restored)
        return 0;
    p->installed = false;
    p->slot = nullptr;
    current.store(nullptr, std::memory_order_release);
    return 1;
}

extern "C" void doom_interaction_presentation_destroy(DoomInteractionPresentation *p)
{
    if (!p) return;
    (void)doom_interaction_presentation_uninstall(p);
    /* A published callback may still be returning on another game thread. */
    if (!p->published) delete p;
}
#else
struct DoomInteractionPresentation {};
extern "C" DoomInteractionPresentation *doom_interaction_presentation_create(void *) { return nullptr; }
extern "C" int doom_interaction_presentation_install(DoomInteractionPresentation *) { return 0; }
extern "C" void doom_interaction_presentation_set_active(DoomInteractionPresentation *, int) {}
extern "C" int doom_interaction_presentation_uninstall(DoomInteractionPresentation *) { return 1; }
extern "C" void doom_interaction_presentation_destroy(DoomInteractionPresentation *) {}
#endif
