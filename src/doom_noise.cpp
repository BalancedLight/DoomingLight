#include "doom_noise.h"

#if defined(DOOM_MODE_LAB) && defined(_DEBUG) && !defined(NDEBUG)
#include <windows.h>
#include <cmath>
#include <cstring>
#include <new>

namespace {
struct Vec3 { float x, y, z; };
using GetPlayer = void *(__fastcall *)(void *);
using GetLevel = void *(__fastcall *)(void *);
using GetVector = Vec3 *(__fastcall *)(void *, Vec3 *);
using SoundMade = void (__fastcall *)(void *, void *, int, const Vec3 *, float,
                                      const Vec3 *, int, float);

bool finite(const Vec3 &v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
int sound_type(DoomWeapon weapon) {
    switch (weapon) {
    case DOOM_FIST: return 5; /* MELEE_ATTACK */
    case DOOM_PISTOL: case DOOM_CHAINGUN: case DOOM_PLASMA: case DOOM_CHAINSAW:
        return 30; /* LIGHT_SHOT */
    case DOOM_SHOTGUN: case DOOM_SUPER_SHOTGUN: case DOOM_ROCKET: case DOOM_BFG:
        return 29; /* HEAVY_SHOT */
    default: return -1;
    }
}
}

struct DoomNoise {
    unsigned char *game;
    GetLevel get_level;
    void *last_actor, *last_level;
    ULONGLONG last_saw_ms;
};

extern "C" DoomNoise *doom_noise_create(void *game_module, void *engine_module) {
    auto *game=static_cast<unsigned char *>(game_module);
    auto engine=static_cast<HMODULE>(engine_module);
    if (!game || !engine || GetModuleHandleW(L"gamedll_x64_rwdi.dll")!=game_module ||
        GetModuleHandleW(L"engine_x64_rwdi.dll")!=engine_module) return nullptr;
    static const unsigned char report_prefix[] =
        {0x48,0x85,0xd2,0x0f,0x84,0x33,0x05,0x00,0x00,0x44,0x89,0x44,0x24,0x18,0x55,0x57};
    static const unsigned char position_prefix[] =
        {0x8b,0x81,0x18,0x08,0x00,0x00,0x89,0x02,0x8b,0x81,0x1c,0x08,0x00,0x00};
    static const unsigned char facing_prefix[] =
        {0x40,0x53,0x48,0x83,0xec,0x20,0xf3,0x0f,0x10,0x91,0x90,0x11,0x00,0x00};
    __try {
        auto *engine_base=reinterpret_cast<unsigned char *>(engine);
        auto level=reinterpret_cast<GetLevel>(GetProcAddress(engine,
            "?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
        auto **table=reinterpret_cast<void **>(game+0x17dd738);
        if (reinterpret_cast<void *>(level)!=engine_base+0x277a00 ||
            table[0x270/sizeof(void *)]!=game+0x58dc80 ||
            table[0x748/sizeof(void *)]!=game+0xb12950 ||
            std::memcmp(game+0x128060,report_prefix,sizeof(report_prefix)) ||
            std::memcmp(game+0x58dc80,position_prefix,sizeof(position_prefix)) ||
            std::memcmp(game+0xb12950,facing_prefix,sizeof(facing_prefix))) return nullptr;
        auto *result=new (std::nothrow) DoomNoise{};
        if (result) { result->game=game; result->get_level=level; }
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

extern "C" void doom_noise_destroy(DoomNoise *noise) { delete noise; }

extern "C" int doom_noise_report(DoomNoise *noise, void *actor, void *level,
                                  DoomWeapon weapon) {
    if (!noise || !actor || !level) return -1;
    const int type=sound_type(weapon);
    if (type<0) return -1;
    __try {
        auto *game=noise->game;
        void *game_object=*reinterpret_cast<void **>(game+0x1d07f88);
        auto get_player=reinterpret_cast<GetPlayer>(game+0x3c2d90);
        if (!game_object || *reinterpret_cast<void **>(game_object)!=game+0x157d5f8 ||
            get_player(game_object)!=actor || *reinterpret_cast<void **>(actor)!=game+0x17dd738 ||
            *reinterpret_cast<void **>(static_cast<unsigned char *>(actor)+40)!=game+0x17de468 ||
            *reinterpret_cast<void **>(level)!=game+0x1581c98 ||
            noise->get_level(static_cast<unsigned char *>(actor)+40)!=level) return -1;
        void *manager=*reinterpret_cast<void **>(static_cast<unsigned char *>(level)+0x9f0);
        if (!manager || *reinterpret_cast<void **>(manager)!=game+0x14e7af8) return -1;
        if (noise->last_actor!=actor || noise->last_level!=level) {
            noise->last_actor=actor; noise->last_level=level; noise->last_saw_ms=0;
        }
        const ULONGLONG now=GetTickCount64();
        if (weapon==DOOM_CHAINSAW && noise->last_saw_ms &&
            now>=noise->last_saw_ms && now-noise->last_saw_ms<250) return 0;
        Vec3 position{}, direction{};
        auto **table=*reinterpret_cast<void ***>(actor);
        auto get_position=reinterpret_cast<GetVector>(table[0x270/sizeof(void *)]);
        auto get_facing=reinterpret_cast<GetVector>(table[0x748/sizeof(void *)]);
        if (reinterpret_cast<void *>(get_position)!=game+0x58dc80 ||
            reinterpret_cast<void *>(get_facing)!=game+0xb12950 ||
            get_position(actor,&position)!=&position || get_facing(actor,&direction)!=&direction ||
            !finite(position) || !finite(direction) || get_player(game_object)!=actor ||
            noise->get_level(static_cast<unsigned char *>(actor)+40)!=level ||
            *reinterpret_cast<void **>(static_cast<unsigned char *>(level)+0x9f0)!=manager) return -1;
        /* Match the game's firearm stimulus: player attribution, WEAPON_USE,
         * 60 metre radius, facing, item noise type, and its -1 modifier. */
        reinterpret_cast<SoundMade>(game+0x128060)(manager,actor,2,&position,60.0f,&direction,type,-1.0f);
        if (weapon==DOOM_CHAINSAW) noise->last_saw_ms=now;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
#else
struct DoomNoise {};
extern "C" DoomNoise *doom_noise_create(void *, void *) { return nullptr; }
extern "C" void doom_noise_destroy(DoomNoise *) {}
extern "C" int doom_noise_report(DoomNoise *, void *, void *, DoomWeapon) { return -1; }
#endif
