#include "doom_movement_sampler.h"
#include <windows.h>
#include <bcrypt.h>
#include <cmath>
#include <cstring>
#include <new>

#if defined(DOOM_MODE_LAB) && defined(DOOM_MOVEMENT_OBSERVER) && defined(_DEBUG) && !defined(NDEBUG)
namespace
{
struct Vec3
{
    float x, y, z;
};
using GetLocal = void *(__fastcall *)(void *);
using GetLevel = void *(__fastcall *)(void *);
using GetId = int(__fastcall *)(void *);
using GetLevelObject = void *(__fastcall *)(void *, int);
using GetOwner = void *(__fastcall *)(void *);
using PhysicsBind = void *(__fastcall *)(void *);
using GetWalk = void *(__fastcall *)(void *);
using GetVector = const Vec3 *(__fastcall *)(void *);
using GetScalar = float(__fastcall *)(void *);
using GetBool = unsigned char(__fastcall *)(void *);
template <class T> T field(void *p, size_t offset)
{
    return *reinterpret_cast<T *>(static_cast<unsigned char *>(p) + offset);
}
static bool exact_hash(HMODULE module, const char *expected)
{
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(module, path, 32768);
    if (!length || length >= 32768)
        return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32]{}, bytes[65536]{};
    DWORD count = 0;
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok)
        ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    while (ok)
    {
        if (!ReadFile(file, bytes, sizeof(bytes), &count, nullptr))
        {
            ok = false;
            break;
        }
        if (!count)
            break;
        ok = BCryptHashData(hash, bytes, count, 0) >= 0;
    }
    if (ok)
        ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash)
        BCryptDestroyHash(hash);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(file);
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned i = 0; ok && i < sizeof(digest); ++i)
        ok = expected[2 * i] == hex[digest[i] >> 4] && expected[2 * i + 1] == hex[digest[i] & 15];
    return ok;
}
} // namespace
struct DoomMovementSampler
{
    unsigned char *game = nullptr, *engine = nullptr;
    GetLocal get_local = nullptr;
    GetLevel get_level = nullptr;
    GetId get_id = nullptr;
    GetLevelObject get_object = nullptr;
    PhysicsBind get_bind = nullptr;
    DoomMovementSamplerError error = DOOM_SAMPLER_OK;
};
extern "C" DoomMovementSampler *doom_movement_sampler_create(void *game, void *engine, int verified)
{
    if (!verified || !game || !engine || GetModuleHandleW(L"gamedll_x64_rwdi.dll") != game ||
        GetModuleHandleW(L"engine_x64_rwdi.dll") != engine)
        return nullptr;
    if (!exact_hash(static_cast<HMODULE>(game),
                    "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95") ||
        !exact_hash(static_cast<HMODULE>(engine),
                    "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C"))
        return nullptr;
    auto *s = new (std::nothrow) DoomMovementSampler();
    if (!s)
        return nullptr;
    s->game = static_cast<unsigned char *>(game);
    s->engine = static_cast<unsigned char *>(engine);
    s->get_local = reinterpret_cast<GetLocal>(s->game + 0x3c2d90);
    s->get_level = reinterpret_cast<GetLevel>(
        GetProcAddress(static_cast<HMODULE>(engine), "?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
    s->get_id = reinterpret_cast<GetId>(
        GetProcAddress(static_cast<HMODULE>(engine), "?GetID@IGSObject@@QEBAHXZ"));
    s->get_object = reinterpret_cast<GetLevelObject>(GetProcAddress(
        static_cast<HMODULE>(engine), "?GetObjectByID@ILevel@@QEBAPEAVIGSObject@@H@Z"));
    s->get_bind = reinterpret_cast<PhysicsBind>(GetProcAddress(
        static_cast<HMODULE>(engine), "?PhysicsBind@IControlObject@@QEAAPEAVIPhysicsBind@@XZ"));
    if (!s->get_level || !s->get_id || !s->get_object ||
        s->get_bind != reinterpret_cast<PhysicsBind>(s->engine + 0x141870))
    {
        delete s;
        return nullptr;
    }
    return s;
}
extern "C" int doom_movement_sampler_read(DoomMovementSampler *s, void *actor, void *level,
                                          uint32_t generation, uint32_t thread_id, uint64_t qpc,
                                          const float position[3], uint32_t move_state, int parkour,
                                          DoomMovementSnapshot *out)
{
    if (!s) return 0;
    s->error = DOOM_SAMPLER_ARGUMENT;
    if (!actor || !level || !out || !position || !thread_id ||
        thread_id != GetCurrentThreadId() || !std::isfinite(position[0]) ||
        !std::isfinite(position[1]) || !std::isfinite(position[2]))
        return 0;
    DoomMovementSnapshot sample{};
    __try
    {
        s->error = DOOM_SAMPLER_ACTOR;
        void *game = *reinterpret_cast<void **>(s->game + 0x1d07f88);
        if (!game || field<void *>(game, 0) != s->game + 0x157d5f8 || s->get_local(game) != actor ||
            field<void *>(actor, 0) != s->game + 0x17dd738 ||
            field<void *>(actor, 24) != s->game + 0x17de1c8 ||
            field<void *>(actor, 40) != s->game + 0x17de468 ||
            s->get_level(static_cast<unsigned char *>(actor) + 40) != level)
            return 0;
        s->error = DOOM_SAMPLER_ACTOR_ID;
        int actor_id = s->get_id(static_cast<unsigned char *>(actor) + 40);
        if (actor_id < 0 ||
            s->get_object(level, actor_id) != static_cast<unsigned char *>(actor) + 40)
            return 0;
        s->error = DOOM_SAMPLER_CONTROLLER;
        void *controller = field<void *>(actor, 0xbc0);
        if (!controller || field<void *>(controller, 0) != s->game + 0x180b368 ||
            (*reinterpret_cast<void ***>(controller))[4] != s->game + 0x106b80 ||
            reinterpret_cast<GetOwner>((*reinterpret_cast<void ***>(controller))[4])(controller) !=
                actor ||
            field<void *>(controller, 24) != s->game + 0x180b488)
            return 0;
        s->error = DOOM_SAMPLER_BIND;
        void *bind = s->get_bind(static_cast<unsigned char *>(actor) + 24);
        if (!bind || field<void *>(bind, 0) != s->engine + 0x8b1158 ||
            (*reinterpret_cast<void ***>(bind))[22] != s->engine + 0x3830c0)
            return 0;
        s->error = DOOM_SAMPLER_PHYSICS;
        void *walk = reinterpret_cast<GetWalk>((*reinterpret_cast<void ***>(bind))[22])(bind);
        if (!walk || field<void *>(walk, 0) != s->engine + 0x8f3c88)
            return 0;
        s->error = DOOM_SAMPLER_GETTERS;
        auto **v = *reinterpret_cast<void ***>(walk);
        if (v[91] != s->engine + 0x43a440 || v[97] != s->engine + 0x43acc0 ||
            v[99] != s->engine + 0x43aeb0 || v[141] != s->engine + 0x43bcb0)
            return 0;
        const Vec3 *wanted = reinterpret_cast<GetVector>(v[91])(walk);
        if (!wanted ||
            wanted != reinterpret_cast<Vec3 *>(static_cast<unsigned char *>(walk) + 0x370))
            return 0;
        sample.wanted_world_speed[0] = wanted->x;
        sample.wanted_world_speed[1] = wanted->y;
        sample.wanted_world_speed[2] = wanted->z;
        sample.walk_acceleration = reinterpret_cast<GetScalar>(v[97])(walk);
        sample.walk_deceleration = reinterpret_cast<GetScalar>(v[99])(walk);
        sample.in_air = reinterpret_cast<GetBool>(v[141])(walk) != 0;
        s->error = DOOM_SAMPLER_VALUES;
        if (!std::isfinite(sample.wanted_world_speed[0]) ||
            !std::isfinite(sample.wanted_world_speed[1]) ||
            !std::isfinite(sample.wanted_world_speed[2]) ||
            !std::isfinite(sample.walk_acceleration) || !std::isfinite(sample.walk_deceleration))
            return 0;
        s->error = DOOM_SAMPLER_CHANGED;
        if (s->get_local(game) != actor ||
            s->get_level(static_cast<unsigned char *>(actor) + 40) != level ||
            s->get_object(level, actor_id) != static_cast<unsigned char *>(actor) + 40 ||
            field<void *>(actor, 0xbc0) != controller ||
            field<void *>(controller, 0) != s->game + 0x180b368 ||
            (*reinterpret_cast<void ***>(controller))[4] != s->game + 0x106b80 ||
            reinterpret_cast<GetOwner>((*reinterpret_cast<void ***>(controller))[4])(controller) != actor ||
            s->get_bind(static_cast<unsigned char *>(actor) + 24) != bind ||
            field<void *>(bind, 0) != s->engine + 0x8b1158 ||
            (*reinterpret_cast<void ***>(bind))[22] != s->engine + 0x3830c0 ||
            reinterpret_cast<GetWalk>((*reinterpret_cast<void ***>(bind))[22])(bind) != walk)
            return 0;
        sample.qpc = qpc;
        sample.generation = generation;
        sample.thread_id = thread_id;
        sample.move_state = move_state;
        sample.parkour = parkour;
        sample.actor_id = actor_id;
        sample.actor_identity = reinterpret_cast<uintptr_t>(actor);
        sample.controller_identity = reinterpret_cast<uintptr_t>(controller);
        sample.physics_identity = reinterpret_cast<uintptr_t>(walk);
        std::memcpy(sample.position, position, sizeof(sample.position));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        s->error = DOOM_SAMPLER_ACCESS;
        return 0;
    }
    s->error = DOOM_SAMPLER_OK;
    *out = sample;
    return 1;
}
extern "C" DoomMovementSamplerError doom_movement_sampler_last_error(const DoomMovementSampler *s)
{
    return s ? s->error : DOOM_SAMPLER_ARGUMENT;
}
extern "C" void doom_movement_sampler_destroy(DoomMovementSampler *s)
{
    delete s;
}
#else
struct DoomMovementSampler
{
};
extern "C" DoomMovementSampler *doom_movement_sampler_create(void *, void *, int)
{
    return nullptr;
}
extern "C" int doom_movement_sampler_read(DoomMovementSampler *, void *, void *, uint32_t, uint32_t,
                                          uint64_t, const float *, uint32_t, int,
                                          DoomMovementSnapshot *)
{
    return 0;
}
extern "C" void doom_movement_sampler_destroy(DoomMovementSampler *)
{
}
extern "C" DoomMovementSamplerError doom_movement_sampler_last_error(const DoomMovementSampler *)
{
    return DOOM_SAMPLER_ARGUMENT;
}
#endif
