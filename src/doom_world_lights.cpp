#include "doom_world_lights.h"
#include <windows.h>
#include <cmath>
#include <cstring>

#ifdef DOOM_MODE_LAB
namespace {
using GetFx = void *(__fastcall *)(void *);
using CreateDef = void *(__fastcall *)(void **);
using ParseDef = unsigned char (__fastcall *)(void *, const char *, const char *);
using ReleaseDef = void (__fastcall *)(void *, void *, void *);
using CreateLight = unsigned (__fastcall *)(void *, void *, const float *, const float *, void *, int);
using MoveFx = unsigned char (__fastcall *)(void *, unsigned, const float *, const float *, const float *);
using StopFx = unsigned char (__fastcall *)(void *, unsigned);
using KillFx = void (__fastcall *)(void *, unsigned);
struct LightSlot { unsigned handle, token; };
volatile LONG next_token = 0;
bool valid_position(const float *p) {
    return p && std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}
}
struct DoomWorldLights {
    unsigned char *base;
    void *level, *fx, *defs, *plasma, *bfg;
    uint32_t generation;
    bool quarantined;
    LightSlot slots[8];
};
namespace {
bool valid(DoomWorldLights *l, void *level, uint32_t generation) {
    if (!l || l->quarantined || !level || l->level != level || l->generation != generation) return false;
    __try {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(l->base), &module) || module != reinterpret_cast<HMODULE>(l->base)) return false;
        return reinterpret_cast<GetFx>(l->base + 0x2dded0)(level) == l->fx &&
            *reinterpret_cast<void **>(l->base + 0xaccc40) == l->defs &&
            *reinterpret_cast<void **>(l->fx) == l->base + 0x8e26f8;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void release_def(DoomWorldLights *l, void *&def) {
    if (def) {
        reinterpret_cast<ReleaseDef>(l->base + 0x204660)(l->defs, def,
            static_cast<unsigned char *>(l->defs) + 240);
        def = nullptr;
    }
}
void clear_slot(DoomWorldLights *l, LightSlot &slot) {
    if (slot.handle) {
        auto **v = *reinterpret_cast<void ***>(l->fx);
        reinterpret_cast<StopFx>(v[2])(l->fx, slot.handle);
        reinterpret_cast<KillFx>(v[35])(l->fx, slot.handle);
    }
    slot = {};
}
}
extern "C" DoomWorldLights *doom_world_lights_create(void *module, void *level, uint32_t generation) {
    if (!module || !level) return nullptr;
    auto *l = static_cast<DoomWorldLights *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DoomWorldLights)));
    if (!l) return nullptr;
    l->base = static_cast<unsigned char *>(module); l->level = level; l->generation = generation;
    bool success = false;
    __try {
        static const struct { unsigned rva; unsigned char bytes[12]; } checks[] = {
            {0x2dded0,{0x48,0x8b,0x41,0x08,0x48,0x05,0xb8,0,0,0,0xc3,0xcc}},
            {0x1fd950,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xb9,0xd8,0}},
            {0x20d560,{0x40,0x56,0x41,0x55,0x41,0x56,0x48,0x83,0xec,0x60,0x48,0x8b}},
            {0x1ea440,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x18,0x48,0x89}},
            {0x204660,{0x48,0x85,0xd2,0x0f,0x84,0xfb,0,0,0,0x48,0x89,0x6c}},
        };
        bool matches = true;
        for (const auto &check : checks) if (std::memcmp(l->base + check.rva, check.bytes, sizeof check.bytes)) { matches = false; break; }
        if (matches) {
            l->fx = reinterpret_cast<GetFx>(l->base + 0x2dded0)(level);
            l->defs = *reinterpret_cast<void **>(l->base + 0xaccc40);
            if (l->fx && l->defs && *reinterpret_cast<void **>(l->fx) == l->base + 0x8e26f8) {
                auto **v = *reinterpret_cast<void ***>(l->fx);
                if (v[44] == l->base + 0x1ea440 && v[70] == l->base + 0x1ec770 &&
                    v[35] == l->base + 0x1e72a0 && v[2] == l->base + 0x1e23e0) {
                    auto create = reinterpret_cast<CreateDef>(l->base + 0x1fd950);
                    auto parse = reinterpret_cast<ParseDef>(l->base + 0x20d560);
                    create(&l->plasma); create(&l->bfg);
                    static const char plasma[] = "LightDef(){ Meter() LifeTime(){keyf(0,600)} Scale(){keyv(0,2,2,2)} Colour(){keyv(0,0.2,0.5,1)} Intensity(){keyf(0,1)} TurnOffDistance(){keyf(0,25)} }";
                    static const char bfg[] = "LightDef(){ Meter() LifeTime(){keyf(0,600)} Scale(){keyv(0,3,3,3)} Colour(){keyv(0,0.2,1,0.2)} Intensity(){keyf(0,1)} TurnOffDistance(){keyf(0,25)} }";
                    success = l->plasma && l->bfg && parse(l->plasma, "doom_plasma_light.fx", plasma) &&
                              parse(l->bfg, "doom_bfg_light.fx", bfg);
                }
            }
        }
        if (!success && (l->plasma || l->bfg)) {
            if (!valid(l, level, generation)) {
                l->quarantined = true;
                return l;
            }
            release_def(l, l->plasma); release_def(l, l->bfg);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        l->quarantined = true;
        return l;
    }
    if (!success) { HeapFree(GetProcessHeap(), 0, l); return nullptr; }
    return l;
}
extern "C" uint32_t doom_world_lights_add(DoomWorldLights *l, void *level, uint32_t generation,
                                           const float position[3], int bfg) {
    if (!valid_position(position) || !valid(l, level, generation)) return 0;
    __try {
        for (auto &slot : l->slots) if (!slot.token) {
            auto **v = *reinterpret_cast<void ***>(l->fx);
            const float up[3] = {0, 1, 0};
            unsigned handle = reinterpret_cast<CreateLight>(v[44])(l->fx, bfg ? l->bfg : l->plasma, position, up, nullptr, -1);
            if (!handle) return 0;
            unsigned token = static_cast<unsigned>(InterlockedIncrement(&next_token));
            if (!token) token = static_cast<unsigned>(InterlockedIncrement(&next_token));
            slot = {handle, token};
            return token;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { l->quarantined = true; }
    return 0;
}
extern "C" int doom_world_lights_move(DoomWorldLights *l, void *level, uint32_t generation,
                                        uint32_t token, const float position[3]) {
    if (!token || !valid_position(position) || !valid(l, level, generation)) return 0;
    __try {
        for (auto &slot : l->slots) if (slot.token == token) {
            auto **v = *reinterpret_cast<void ***>(l->fx);
            return reinterpret_cast<MoveFx>(v[70])(l->fx, slot.handle, position, nullptr, nullptr) != 0;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { l->quarantined = true; }
    return 0;
}
extern "C" int doom_world_lights_remove(DoomWorldLights *l, void *level, uint32_t generation, uint32_t token) {
    if (!token || !valid(l, level, generation)) return 0;
    __try { for (auto &slot : l->slots) if (slot.token == token) { clear_slot(l, slot); return 1; } }
    __except(EXCEPTION_EXECUTE_HANDLER) { l->quarantined = true; }
    return 0;
}
extern "C" int doom_world_lights_destroy(DoomWorldLights *l, void *level, uint32_t generation) {
    if (!l) return 1;
    if (!valid(l, level, generation)) return 0;
    __try {
        for (auto &slot : l->slots) if (slot.token) clear_slot(l, slot);
        release_def(l, l->plasma); release_def(l, l->bfg);
    } __except(EXCEPTION_EXECUTE_HANDLER) { l->quarantined = true; return 0; }
    HeapFree(GetProcessHeap(), 0, l);
    return 1;
}
#else
struct DoomWorldLights {};
extern "C" DoomWorldLights *doom_world_lights_create(void *, void *, uint32_t) { return nullptr; }
extern "C" uint32_t doom_world_lights_add(DoomWorldLights *, void *, uint32_t, const float *, int) { return 0; }
extern "C" int doom_world_lights_move(DoomWorldLights *, void *, uint32_t, uint32_t, const float *) { return 0; }
extern "C" int doom_world_lights_remove(DoomWorldLights *, void *, uint32_t, uint32_t) { return 0; }
extern "C" int doom_world_lights_destroy(DoomWorldLights *, void *, uint32_t) { return 1; }
#endif
