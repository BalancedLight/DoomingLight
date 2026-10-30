#include "doom_viewmodel.h"
#include <windows.h>
#include <MinHook.h>
#include <cstring>

namespace {
using Update = void (__fastcall *)(void *, unsigned);
using GetPlayer = void *(__fastcall *)(void *);
using Visible = unsigned char (__fastcall *)(void *, unsigned);
using IsRenderable = unsigned char (__fastcall *)(void *);
using EnableRendering = void (__fastcall *)(void *, unsigned char);
struct State {
    unsigned char *game; Update original; Visible desired; IsRenderable is_renderable; EnableRendering render;
    SRWLOCK lock; volatile LONG active, calls; bool installed, enabled;
    void *visual, *weapons[2]; bool owned[2];
};
State state = {};
void *player() {
    void *g = *reinterpret_cast<void **>(state.game + 0x1d07f88);
    return g ? reinterpret_cast<GetPlayer>(state.game + 0x3c2d90)(g) : nullptr;
}
void *local_visual() {
    void *p = player(); if (!p) return nullptr;
    auto table = *reinterpret_cast<void ***>(p);
    if (table != reinterpret_cast<void **>(state.game + 0x17dd738) || table[194] != state.game + 0x106b90) return nullptr;
    void *visual = reinterpret_cast<GetPlayer>(table[194])(p); if (!visual) return nullptr;
    if (*reinterpret_cast<void **>(visual) != state.game + 0x17e1188 || *reinterpret_cast<void **>(static_cast<unsigned char *>(visual) + 80) != p) return nullptr;
    return visual;
}
void __fastcall update_hook(void *visual, unsigned hand) {
    InterlockedIncrement(&state.calls);
    Update original = state.original;
    if (original) original(visual, hand);
    if (hand < 2 && InterlockedCompareExchange(&state.active,0,0)) {
        AcquireSRWLockExclusive(&state.lock);
        __try {
            if (InterlockedCompareExchange(&state.active,0,0) && visual == local_visual()) {
                void *weapon = *reinterpret_cast<void **>(static_cast<unsigned char *>(visual) + 0x1068 + hand * sizeof(void *));
                if (state.visual != visual) { state.visual=visual; std::memset(state.weapons,0,sizeof(state.weapons)); std::memset(state.owned,0,sizeof(state.owned)); }
                if (state.weapons[hand] != weapon) { state.weapons[hand]=weapon; state.owned[hand]=false; }
                if (weapon) {
                    bool desired = state.desired(visual,hand) != 0;
                    if (!desired) state.owned[hand]=false;
                    else if (state.is_renderable(weapon)) { state.render(weapon,0); state.owned[hand]=true; }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        ReleaseSRWLockExclusive(&state.lock);
    }
    InterlockedDecrement(&state.calls);
}
}
extern "C" int doom_viewmodel_start(void *game_module, void *engine_module) {
    static const unsigned char update_bytes[] = {0x40,0x55,0x57,0x41,0x55,0x48,0x83,0xec,0x20,0x48,0x8b,0x69,0x50,0x4c,0x63,0xea};
    static const unsigned char visible_bytes[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48};
    auto *game = static_cast<unsigned char *>(game_module); HMODULE engine = static_cast<HMODULE>(engine_module);
    if (!game || !engine || (state.installed && state.game != game)) return 0;
    __try {
        if (std::memcmp(game + 0xbe0220,update_bytes,sizeof(update_bytes)) || std::memcmp(game + 0xc096a0,visible_bytes,sizeof(visible_bytes))) return 0;
        auto is_renderable = reinterpret_cast<IsRenderable>(GetProcAddress(engine,"?IsRenderable@IModelObject@@QEBA_NXZ"));
        auto render = reinterpret_cast<EnableRendering>(GetProcAddress(engine,"?EnableRendering@IModelObject@@QEAAX_N@Z"));
        if (reinterpret_cast<void *>(is_renderable) != reinterpret_cast<unsigned char *>(engine)+0x14aa80 || reinterpret_cast<void *>(render) != reinterpret_cast<unsigned char *>(engine)+0x14ea70) return 0;
        state.game=game; state.desired=reinterpret_cast<Visible>(game+0xc096a0); state.is_renderable=is_renderable; state.render=render;
        if (!state.installed) {
            MH_STATUS init=MH_Initialize(); if (init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) return 0;
            if (MH_CreateHook(game+0xbe0220,reinterpret_cast<void *>(&update_hook),reinterpret_cast<void **>(&state.original))!=MH_OK) return 0;
            state.installed=true;
        }
        if (!state.enabled) { if(MH_EnableHook(game+0xbe0220)!=MH_OK) return 0; state.enabled=true; }
        InterlockedExchange(&state.active,1); return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
extern "C" int doom_viewmodel_suspend(void) {
    InterlockedExchange(&state.active,0);
    if (!state.installed) return 1;
    if (state.enabled) {
        if (MH_DisableHook(state.game+0xbe0220)!=MH_OK) return 0;
        state.enabled=false;
    }
    return 1;
}
extern "C" int doom_viewmodel_stop(void) {
    if (!doom_viewmodel_suspend()) return 0;
    if (!state.installed) return 1;
    if (InterlockedCompareExchange(&state.calls,0,0)!=0) return 0;
    AcquireSRWLockExclusive(&state.lock);
    __try {
        void *visual=local_visual();
        if (visual && visual==state.visual) for(unsigned hand=0;hand<2;++hand) {
            void *weapon=*reinterpret_cast<void **>(static_cast<unsigned char *>(visual)+0x1068+hand*sizeof(void *));
            if (state.owned[hand] && weapon && weapon==state.weapons[hand] && state.desired(visual,hand) && !state.is_renderable(weapon)) state.render(weapon,1);
        }
        state.visual=nullptr; std::memset(state.weapons,0,sizeof(state.weapons)); std::memset(state.owned,0,sizeof(state.owned));
    } __except(EXCEPTION_EXECUTE_HANDLER) { ReleaseSRWLockExclusive(&state.lock); return 0; }
    ReleaseSRWLockExclusive(&state.lock);
    return 1;
}
