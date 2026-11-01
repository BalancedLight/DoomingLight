#include "doom_death_presentation.h"
#include <windows.h>
#include <MinHook.h>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
struct Vec3 { float x,y,z; };
using CameraUpdate = uintptr_t (__fastcall *)(void *);
using GetVector = Vec3 *(__fastcall *)(void *, Vec3 *);
using SetTransform = void (__fastcall *)(void *, const Vec3 &, const Vec3 &, const Vec3 &);
using GetPlayer = void *(__fastcall *)(void *);
struct State {
    unsigned char *game; CameraUpdate original;
    GetVector position,forward,up,actor_position; SetTransform transform;
    CRITICAL_SECTION lock; bool lock_ready,installed,enabled;
    volatile LONG calls; void *camera,*actor,*saved_camera,*saved_actor;
    uint32_t generation; volatile LONG active; int view_height; bool overridden;
    Vec3 saved_position,saved_forward,saved_up;
    Vec3 displayed_position,displayed_forward,displayed_up;
    Vec3 held_position,held_forward,held_up;
    void *displayed_actor,*held_actor; uint32_t displayed_generation,held_generation;
    bool displayed,held;
};
State state = {};
bool valid(void *camera, void *actor) {
    if (!state.game || !camera || !actor) return false;
    void *game = *reinterpret_cast<void **>(state.game+0x1d07f88);
    if (!game || reinterpret_cast<GetPlayer>(state.game+0x3c2d90)(game)!=actor ||
        *reinterpret_cast<void **>(actor)!=state.game+0x17dd738 ||
        *reinterpret_cast<void **>(camera)!=state.game+0x15590c8) return false;
    void *session=*reinterpret_cast<void **>(static_cast<unsigned char *>(game)+1288);
    void *manager=session ? *reinterpret_cast<void **>(static_cast<unsigned char *>(session)+192) : nullptr;
    if (!manager || *reinterpret_cast<void **>(static_cast<unsigned char *>(manager)+80)!=camera) return false;
    int mode=*reinterpret_cast<int *>(static_cast<unsigned char *>(manager)+88);
    return mode==1 || mode==2;
}
bool finite(const Vec3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool restore() {
    if (!state.overridden) return true;
    void *game=*reinterpret_cast<void **>(state.game+0x1d07f88);
    if (!game) return false;
    void *actor=reinterpret_cast<GetPlayer>(state.game+0x3c2d90)(game);
    void *session=*reinterpret_cast<void **>(static_cast<unsigned char *>(game)+1288);
    void *manager=session ? *reinterpret_cast<void **>(static_cast<unsigned char *>(session)+192) : nullptr;
    if (!actor || !manager) return false;
    void *camera=*reinterpret_cast<void **>(static_cast<unsigned char *>(manager)+80);
    if (!camera) return false;
    if (camera==state.saved_camera && actor==state.saved_actor) {
        if (!valid(camera,actor)) return false;
        Vec3 forward={-state.saved_forward.x,-state.saved_forward.y,-state.saved_forward.z};
        state.transform(camera,forward,state.saved_up,state.saved_position);
    }
    state.overridden=false; state.saved_camera=nullptr; state.saved_actor=nullptr;
    return true;
}
uintptr_t __fastcall update_hook(void *camera) {
    InterlockedIncrement(&state.calls);
    uintptr_t result=0;
    EnterCriticalSection(&state.lock);
    __try {
        if (state.overridden && camera==state.saved_camera) (void)restore();
        result=state.original(camera);
        if (InterlockedCompareExchange(&state.active,0,0) && !state.overridden && camera==state.camera && valid(camera,state.actor)) {
            Vec3 pos={},forward={},up={},actor_pos={};
            const bool held=state.held && state.held_actor==state.actor && state.held_generation==state.generation;
            if (state.position(camera,&pos) && state.forward(camera,&forward) && state.up(camera,&up) &&
                finite(pos) && finite(forward) && finite(up) &&
                (held || (state.actor_position(static_cast<unsigned char *>(state.actor)+24,&actor_pos) &&
                          finite(actor_pos) && pos.y-actor_pos.y >= 0.1875f && pos.y-actor_pos.y <= 2.5f))) {
                state.saved_camera=camera; state.saved_actor=state.actor;
                state.saved_position=pos; state.saved_forward=forward; state.saved_up=up;
                Vec3 native_forward={-forward.x,-forward.y,-forward.z};
                if (held) {
                    pos=state.held_position; native_forward=state.held_forward; up=state.held_up;
                } else {
                    int height=state.view_height; if (height<6) height=6; if (height>41) height=41;
                    const float target_y=actor_pos.y+0.1875f;
                    pos.y -= (pos.y-target_y)*static_cast<float>(41-height)/35.0f;
                }
                state.transform(camera,native_forward,up,pos); state.overridden=true;
                state.displayed_position=pos; state.displayed_forward=native_forward; state.displayed_up=up;
                state.displayed_actor=state.actor; state.displayed_generation=state.generation; state.displayed=true;
            }
        }
    } __finally { LeaveCriticalSection(&state.lock); InterlockedDecrement(&state.calls); }
    return result;
}
}
extern "C" int doom_death_presentation_start(void *game_module, void *engine_module) {
    static const unsigned char code[] = {0x48,0x8b,0xc4,0x57,0x48,0x81,0xec,0xe0,0x00,0x00,0x00,0x48,0x8b,0xf9,0x48,0x8b,0x49,0x50};
    auto *game=static_cast<unsigned char *>(game_module); HMODULE engine=static_cast<HMODULE>(engine_module);
    if (!game || !engine || (state.installed && state.game!=game)) return 0;
    __try {
        auto table=reinterpret_cast<void **>(game+0x15590c8);
        if (table[9]!=game+0x2bb220 || std::memcmp(game+0x2bb220,code,sizeof(code))) return 0;
        auto position=reinterpret_cast<GetVector>(GetProcAddress(engine,"?GetPosition@IBaseCamera@@QEBA?BVvec3@@XZ"));
        auto forward=reinterpret_cast<GetVector>(GetProcAddress(engine,"?GetForwardVector@IBaseCamera@@QEBA?BVvec3@@XZ"));
        auto up=reinterpret_cast<GetVector>(GetProcAddress(engine,"?GetUpVector@IBaseCamera@@QEBA?BVvec3@@XZ"));
        auto actor_position=reinterpret_cast<GetVector>(GetProcAddress(engine,"?GetWorldPosition@IControlObject@@QEBA?AVvec3@@XZ"));
        auto transform=reinterpret_cast<SetTransform>(GetProcAddress(engine,"?FromForwardUpPos@IBaseCamera@@UEAAXAEBVvec3@@00@Z"));
        auto *base=reinterpret_cast<unsigned char *>(engine);
        if (reinterpret_cast<void *>(position)!=base+0x1264d0 || reinterpret_cast<void *>(forward)!=base+0x126530 ||
            reinterpret_cast<void *>(up)!=base+0x126510 || reinterpret_cast<void *>(transform)!=base+0x125b00 ||
            reinterpret_cast<void *>(actor_position)!=base+0x136af0) return 0;
        state.game=game; state.position=position; state.forward=forward; state.up=up; state.actor_position=actor_position; state.transform=transform;
        if (!state.lock_ready) { InitializeCriticalSection(&state.lock); state.lock_ready=true; }
        if (!state.installed) {
            MH_STATUS init=MH_Initialize(); if (init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) return 0;
            if (MH_CreateHook(game+0x2bb220,reinterpret_cast<void *>(&update_hook),reinterpret_cast<void **>(&state.original))!=MH_OK) return 0;
            state.installed=true;
        }
        if (!state.enabled) { if (MH_EnableHook(game+0x2bb220)!=MH_OK) return 0; state.enabled=true; }
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
extern "C" void doom_death_presentation_set(void *camera, void *actor, uint32_t generation, int dead_wait, int view_height) {
    if (!state.lock_ready) return;
    EnterCriticalSection(&state.lock);
    __try {
        const bool reset_pose=!dead_wait || !generation || actor!=state.actor || generation!=state.generation;
        if ((!dead_wait || !generation || camera!=state.camera || actor!=state.actor || generation!=state.generation) && !restore()) {
            InterlockedExchange(&state.active,0); LeaveCriticalSection(&state.lock); return;
        }
        if (reset_pose) { state.held=false; state.displayed=false; }
        InterlockedExchange(&state.active,dead_wait && camera && actor && generation); state.camera=camera; state.actor=actor;
        state.generation=generation; state.view_height=view_height;
    } __except(EXCEPTION_EXECUTE_HANDLER) { InterlockedExchange(&state.active,0); }
    LeaveCriticalSection(&state.lock);
}
extern "C" void doom_death_presentation_commit(void) {
    if (!state.lock_ready) return;
    EnterCriticalSection(&state.lock);
    if (state.displayed && state.displayed_actor==state.actor &&
        state.displayed_generation==state.generation && InterlockedCompareExchange(&state.active,0,0)) {
        state.held_position=state.displayed_position; state.held_forward=state.displayed_forward;
        state.held_up=state.displayed_up; state.held_actor=state.actor;
        state.held_generation=state.generation; state.held=true;
    }
    LeaveCriticalSection(&state.lock);
}
extern "C" int doom_death_presentation_stop(void) {
    InterlockedExchange(&state.active,0);
    if (!state.installed) return 1;
    if (state.enabled) { if (MH_DisableHook(state.game+0x2bb220)!=MH_OK) return 0; state.enabled=false; }
    if (InterlockedCompareExchange(&state.calls,0,0)) return 0;
    EnterCriticalSection(&state.lock);
    __try { if (!restore()) { LeaveCriticalSection(&state.lock); return 0; } state.camera=nullptr; state.actor=nullptr; state.generation=0; state.held=false; state.displayed=false; }
    __except(EXCEPTION_EXECUTE_HANDLER) { LeaveCriticalSection(&state.lock); return 0; }
    LeaveCriticalSection(&state.lock); return 1;
}
