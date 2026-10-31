#include "doom_hostility.h"
#if defined(DOOM_MODE_RUNTIME) && defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstring>

namespace {
using DynamicCast = void *(__fastcall *)(void *, long, const void *, const void *, int);
using IsEnemy = unsigned char (__fastcall *)(void *, const void *);

int query(void *module, void *target, void *player) {
    auto *game=static_cast<unsigned char *>(module);
    if (!game || !target || !player || target==player) return -1;
    static const unsigned char cast_prefix[]={0xff,0x25,0x64,0x19,0x0c,0x00};
    static const unsigned char enemy_prefix[]={0x40,0x53,0x48,0x83,0xec,0x20,0x80,0xb9,0x18,0x0c,0x00,0x00,0x00,0x48,0x8b,0xda};
    if (std::memcmp(game+0x130A87E,cast_prefix,sizeof(cast_prefix)) ||
        std::memcmp(game+0x1034C0,enemy_prefix,sizeof(enemy_prefix)) ||
        std::strcmp(reinterpret_cast<const char *>(game+0x1C95390+16),".?AVObjectDI@@") ||
        std::strcmp(reinterpret_cast<const char *>(game+0x1CA3FD0+16),".?AVAI@@") ||
        std::strcmp(reinterpret_cast<const char *>(game+0x1CA3FB0+16),".?AVPlayerDI@@")) return -1;
    auto cast=reinterpret_cast<DynamicCast>(game+0x130A87E);
    void *ai=cast(target,0,game+0x1C95390,game+0x1CA3FD0,0);
    void *checked_player=cast(player,0,game+0x1C95390,game+0x1CA3FB0,0);
    if (!ai || !checked_player || checked_player!=player) return -1;
    auto **vtable=*reinterpret_cast<void ***>(ai);
    if (!vtable || vtable[42]!=game+0x1034C0) return -1;
    auto predicate=reinterpret_cast<IsEnemy>(vtable[42]);
    return predicate(ai,player) ? 1 : 0;
}
}
extern "C" int doom_hostility_query(void *game, void *target, void *player) {
    __try { return query(game,target,player); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
#else
extern "C" int doom_hostility_query(void *, void *, void *) { return -1; }
#endif
