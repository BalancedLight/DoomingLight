#include "doom_movement_observer.h"
#include <windows.h>
#include <bcrypt.h>
#include <xmmintrin.h>
#include <atomic>
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
using PhysicsBind = void *(__fastcall *)(void *);
using GetWalk = void *(__fastcall *)(void *);
using GetOwner = void *(__fastcall *)(void *);
using LocalVector = Vec3 *(__fastcall *)(void *, Vec3 *);
using LocalAcceleration = __m128(__fastcall *)(void *);
using LocalDeceleration = float(__fastcall *)(void *);
using WorldVector = void(__fastcall *)(void *, const Vec3 *);
using WorldScalar = void(__fastcall *)(void *, float);
struct Slot
{
    void **address;
    void *original;
    void *replacement;
    bool installed;
    bool protection_failed = false;
};
std::atomic<DoomMotionObserver *> current{nullptr};
LocalVector local_vector = nullptr;
LocalAcceleration local_acceleration = nullptr;
LocalDeceleration local_deceleration = nullptr;
WorldVector world_vector = nullptr;
WorldScalar world_acceleration = nullptr, world_deceleration = nullptr;
constexpr unsigned Capacity = 256;
} // namespace
struct DoomMotionObserver
{
    unsigned char *game = nullptr, *engine = nullptr;
    GetLocal get_local = nullptr;
    GetLevel get_level = nullptr;
    PhysicsBind get_bind = nullptr;
    SRWLOCK lock = SRWLOCK_INIT;
    Slot slots[6]{};
    void *actor = nullptr, *controller = nullptr, *walk = nullptr, *level = nullptr;
    DWORD thread = 0;
    uint32_t generation = 0, move_state = 0;
    int grounded = 0, parkour = 0;
    float position[3]{};
    DoomMotionSample samples[Capacity]{};
    unsigned head = 0, count = 0, dropped = 0;
    bool active = false, published = false;
};
namespace
{
template <class T> T field(void *p, size_t offset)
{
    return *reinterpret_cast<T *>(static_cast<unsigned char *>(p) + offset);
}
static bool exact_hash(HMODULE module, const char *expected)
{
    wchar_t path[32768];
    DWORD len = GetModuleFileNameW(module, path, 32768);
    if (!len || len >= 32768)
        return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32]{}, data[65536]{};
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok)
        ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    DWORD got = 0;
    while (ok)
    {
        if (!ReadFile(file, data, sizeof(data), &got, nullptr))
        {
            ok = false;
            break;
        }
        if (!got)
            break;
        ok = BCryptHashData(hash, data, got, 0) >= 0;
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
        ok = expected[i * 2] == hex[digest[i] >> 4] && expected[i * 2 + 1] == hex[digest[i] & 15];
    return ok;
}
static bool replace_slot(Slot &slot, void *expected, void *replacement)
{
    DWORD old = 0, ignored = 0;
    if (!VirtualProtect(slot.address, sizeof(void *), PAGE_READWRITE, &old))
        return false;
    void *seen = nullptr;
    __try { seen = InterlockedCompareExchangePointer(slot.address, replacement, expected); }
    __except(EXCEPTION_EXECUTE_HANDLER) { seen = nullptr; }
    if (seen == expected)
        slot.installed = replacement == slot.replacement;
    const bool restored = VirtualProtect(slot.address, sizeof(void *), old, &ignored) != 0;
    if (!restored) slot.protection_failed = true;
    return seen == expected && restored;
}
static void capture(DoomMotionObserver *o, void *receiver, DoomMotionSampleKind kind,
                    const float *values, int action)
{
    if (!o || !receiver || !TryAcquireSRWLockExclusive(&o->lock))
        return;
    if (o->active && GetCurrentThreadId() == o->thread &&
        receiver == (kind <= DOOM_MOTION_LOCAL_DECEL ? o->controller : o->walk))
    {
        if (o->count == Capacity)
        {
            ++o->dropped;
        }
        else
        {
            DoomMotionSample &s = o->samples[(o->head + o->count) % Capacity];
            LARGE_INTEGER qpc{};
            QueryPerformanceCounter(&qpc);
            s = {};
            s.qpc = static_cast<uint64_t>(qpc.QuadPart);
            s.generation = o->generation;
            s.thread_id = o->thread;
            s.kind = static_cast<uint32_t>(kind);
            s.move_state = o->move_state;
            s.grounded = o->grounded;
            s.parkour = o->parkour;
            s.input_action = action;
            std::memcpy(s.frame_position, o->position, sizeof(s.frame_position));
            if (values)
                std::memcpy(s.value, values, sizeof(s.value));
            ++o->count;
        }
    }
    ReleaseSRWLockExclusive(&o->lock);
}
static Vec3 *__fastcall local_vector_hook(void *receiver, Vec3 *out)
{
    Vec3 *result = local_vector(receiver, out);
    DoomMotionObserver *o = current.load(std::memory_order_acquire);
    float values[3]{};
    bool copied = false;
    __try
    {
        if (result && result == out) {
            std::memcpy(values, result, sizeof(values));
            copied = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    if (copied)
        capture(o, receiver, DOOM_MOTION_LOCAL_WANTED, values, 0);
    return result;
}
static __m128 __fastcall local_acceleration_hook(void *receiver)
{
    __m128 result = local_acceleration(receiver);
    float values[3] = {_mm_cvtss_f32(result), 0, 0};
    capture(current.load(std::memory_order_acquire), receiver, DOOM_MOTION_LOCAL_ACCEL, values, 0);
    return result;
}
static float __fastcall local_deceleration_hook(void *receiver)
{
    float result = local_deceleration(receiver);
    float values[3] = {result, 0, 0};
    capture(current.load(std::memory_order_acquire), receiver, DOOM_MOTION_LOCAL_DECEL, values, 0);
    return result;
}
static void __fastcall world_vector_hook(void *receiver, const Vec3 *value)
{
    DoomMotionObserver *o = current.load(std::memory_order_acquire);
    float values[3]{};
    bool copied = false;
    __try
    {
        if (value) {
            std::memcpy(values, value, sizeof(values));
            copied = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    if (copied)
        capture(o, receiver, DOOM_MOTION_WORLD_WANTED, values, 0);
    world_vector(receiver, value);
}
static void __fastcall world_acceleration_hook(void *receiver, float value)
{
    float values[3] = {value, 0, 0};
    capture(current.load(std::memory_order_acquire), receiver, DOOM_MOTION_WORLD_ACCEL, values, 0);
    world_acceleration(receiver, value);
}
static void __fastcall world_deceleration_hook(void *receiver, float value)
{
    float values[3] = {value, 0, 0};
    capture(current.load(std::memory_order_acquire), receiver, DOOM_MOTION_WORLD_DECEL, values, 0);
    world_deceleration(receiver, value);
}
} // namespace
extern "C" DoomMotionObserver *doom_motion_observer_create(void *game, void *engine, int verified)
{
    if (!verified || !game || !engine || GetModuleHandleW(L"gamedll_x64_rwdi.dll") != game ||
        GetModuleHandleW(L"engine_x64_rwdi.dll") != engine)
        return nullptr;
    if (!exact_hash(static_cast<HMODULE>(game),
                    "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95") ||
        !exact_hash(static_cast<HMODULE>(engine),
                    "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C"))
        return nullptr;
    auto *o = new (std::nothrow) DoomMotionObserver();
    if (!o)
        return nullptr;
    o->game = static_cast<unsigned char *>(game);
    o->engine = static_cast<unsigned char *>(engine);
    o->get_local = reinterpret_cast<GetLocal>(o->game + 0x3c2d90);
    o->get_level = reinterpret_cast<GetLevel>(
        GetProcAddress(static_cast<HMODULE>(engine), "?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
    o->get_bind = reinterpret_cast<PhysicsBind>(GetProcAddress(
        static_cast<HMODULE>(engine), "?PhysicsBind@IControlObject@@QEAAPEAVIPhysicsBind@@XZ"));
    if (!o->get_level || o->get_bind != reinterpret_cast<PhysicsBind>(o->engine + 0x141870))
    {
        delete o;
        return nullptr;
    }
    return o;
}
extern "C" int doom_motion_observer_start(DoomMotionObserver *o)
{
    if (!o)
        return 0;
    if (current.load(std::memory_order_acquire) == o)
    {
        for (const auto &s : o->slots)
            if (!s.installed || s.protection_failed)
                return 0;
        return 1;
    }
    static const unsigned char localPrefix[] = {0x48, 0x89, 0x5c, 0x24, 0x10,
                                                0x48, 0x89, 0x74, 0x24, 0x18};
    static const unsigned char accelPrefix[] = {0x48, 0x89, 0x5c, 0x24, 0x08,
                                                0x48, 0x89, 0x74, 0x24, 0x10};
    static const unsigned char decelPrefix[] = {0x48, 0x89, 0x5c, 0x24, 0x08,
                                                0x57, 0x48, 0x83, 0xec, 0x30};
    static const unsigned char worldPrefix[] = {0x48, 0x89, 0x74, 0x24, 0x20,
                                                0x57, 0x48, 0x81, 0xec, 0xc0};
    static const unsigned char scalarPrefix[] = {0x48, 0x8b, 0xc4, 0x57, 0x48,
                                                 0x81, 0xec, 0xa0, 0,    0};
    __try
    {
        if (std::memcmp(o->game + 0xce5840, localPrefix, sizeof(localPrefix)) ||
            std::memcmp(o->game + 0xcdb770, accelPrefix, sizeof(accelPrefix)) ||
            std::memcmp(o->game + 0xcdb9b0, decelPrefix, sizeof(decelPrefix)) ||
            std::memcmp(o->engine + 0x44c180, worldPrefix, sizeof(worldPrefix)) ||
            std::memcmp(o->engine + 0x43aae0, scalarPrefix, sizeof(scalarPrefix)) ||
            std::memcmp(o->engine + 0x43acd0, scalarPrefix, sizeof(scalarPrefix)))
            return 0;
        auto **gv = reinterpret_cast<void **>(o->game + 0x180b488);
        auto **ev = reinterpret_cast<void **>(o->engine + 0x8f3c88);
        o->slots[0] = {gv + 0, o->game + 0xce5840, reinterpret_cast<void *>(&local_vector_hook),
                       false};
        o->slots[1] = {gv + 1, o->game + 0xcdb770,
                       reinterpret_cast<void *>(&local_acceleration_hook), false};
        o->slots[2] = {gv + 2, o->game + 0xcdb9b0,
                       reinterpret_cast<void *>(&local_deceleration_hook), false};
        o->slots[3] = {ev + 90, o->engine + 0x44c180, reinterpret_cast<void *>(&world_vector_hook),
                       false};
        o->slots[4] = {ev + 96, o->engine + 0x43aae0,
                       reinterpret_cast<void *>(&world_acceleration_hook), false};
        o->slots[5] = {ev + 98, o->engine + 0x43acd0,
                       reinterpret_cast<void *>(&world_deceleration_hook), false};
        for (auto &s : o->slots)
            if (*s.address != s.original)
                return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    DoomMotionObserver *empty = nullptr;
    if (!current.compare_exchange_strong(empty, o))
        return 0;
    local_vector = reinterpret_cast<LocalVector>(o->slots[0].original);
    local_acceleration = reinterpret_cast<LocalAcceleration>(o->slots[1].original);
    local_deceleration = reinterpret_cast<LocalDeceleration>(o->slots[2].original);
    world_vector = reinterpret_cast<WorldVector>(o->slots[3].original);
    world_acceleration = reinterpret_cast<WorldScalar>(o->slots[4].original);
    world_deceleration = reinterpret_cast<WorldScalar>(o->slots[5].original);
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&local_vector_hook), &self))
    {
        current.store(nullptr);
        return 0;
    }
    o->published = true;
    for (auto &s : o->slots)
        if (!replace_slot(s, s.original, s.replacement))
        {
            (void)doom_motion_observer_stop(o);
            return 0;
        }
    return 1;
}
extern "C" int doom_motion_observer_begin(DoomMotionObserver *o, void *actor, void *level,
                                          uint32_t generation, const float position[3],
                                          uint32_t move_state, int grounded, int parkour)
{
    void *controller = nullptr, *walk = nullptr;
    if (!o)
        return 0;
    doom_motion_observer_end(o);
    if (current.load(std::memory_order_acquire) != o || !actor || !level ||
        !position || !std::isfinite(position[0]) || !std::isfinite(position[1]) ||
        !std::isfinite(position[2]))
        return 0;
    __try
    {
        for (const auto &s : o->slots)
            if (!s.installed || s.protection_failed || *s.address != s.replacement)
                return 0;
        void *game = *reinterpret_cast<void **>(o->game + 0x1d07f88);
        if (!game || *reinterpret_cast<void **>(game) != o->game + 0x157d5f8 ||
            o->get_local(game) != actor || field<void *>(actor, 0) != o->game + 0x17dd738 ||
            field<void *>(actor, 24) != o->game + 0x17de1c8 ||
            field<void *>(actor, 40) != o->game + 0x17de468 ||
            o->get_level(static_cast<unsigned char *>(actor) + 40) != level)
            return 0;
        controller = field<void *>(actor, 0xbc0);
        if (!controller || field<void *>(controller, 0) != o->game + 0x180b368 ||
            (*reinterpret_cast<void ***>(controller))[4] != o->game + 0x106b80 ||
            reinterpret_cast<GetOwner>((*reinterpret_cast<void ***>(controller))[4])(controller) !=
                actor ||
            field<void *>(controller, 24) != o->game + 0x180b488)
            return 0;
        void *bind = o->get_bind(static_cast<unsigned char *>(actor) + 24);
        if (!bind || field<void *>(bind, 0) != o->engine + 0x8b1158 ||
            (*reinterpret_cast<void ***>(bind))[22] != o->engine + 0x3830c0)
            return 0;
        walk = reinterpret_cast<GetWalk>((*reinterpret_cast<void ***>(bind))[22])(bind);
        if (!walk || field<void *>(walk, 0) != o->engine + 0x8f3c88 ||
            o->get_local(game) != actor ||
            o->get_level(static_cast<unsigned char *>(actor) + 40) != level)
            return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    AcquireSRWLockExclusive(&o->lock);
    o->actor = actor;
    o->level = level;
    o->controller = static_cast<unsigned char *>(controller) + 24;
    o->walk = walk;
    o->generation = generation;
    o->thread = GetCurrentThreadId();
    o->move_state = move_state;
    o->grounded = grounded;
    o->parkour = parkour;
    std::memcpy(o->position, position, sizeof(o->position));
    o->active = true;
    ReleaseSRWLockExclusive(&o->lock);
    return 1;
}
extern "C" void doom_motion_observer_note_input(DoomMotionObserver *o, int action, float value)
{
    if (!o || !TryAcquireSRWLockExclusive(&o->lock))
        return;
    /* Input is tagged against the local controller context by the caller. */
    if (o->active && GetCurrentThreadId() == o->thread)
    {
        if (o->count == Capacity)
            ++o->dropped;
        else
        {
            DoomMotionSample &s = o->samples[(o->head + o->count) % Capacity];
            LARGE_INTEGER qpc{};
            QueryPerformanceCounter(&qpc);
            s = {};
            s.qpc = static_cast<uint64_t>(qpc.QuadPart);
            s.generation = o->generation;
            s.thread_id = o->thread;
            s.kind = DOOM_MOTION_INPUT;
            s.move_state = o->move_state;
            s.grounded = o->grounded;
            s.parkour = o->parkour;
            s.input_action = action;
            s.value[0] = value;
            std::memcpy(s.frame_position, o->position, sizeof(s.frame_position));
            ++o->count;
        }
    }
    ReleaseSRWLockExclusive(&o->lock);
}
extern "C" void doom_motion_observer_end(DoomMotionObserver *o)
{
    if (!o)
        return;
    AcquireSRWLockExclusive(&o->lock);
    o->active = false;
    o->actor = o->controller = o->walk = o->level = nullptr;
    o->thread = 0;
    ReleaseSRWLockExclusive(&o->lock);
}
extern "C" unsigned doom_motion_observer_drain(DoomMotionObserver *o, DoomMotionSample *out,
                                               unsigned capacity, unsigned *dropped)
{
    if (!o || (!out && capacity))
        return 0;
    AcquireSRWLockExclusive(&o->lock);
    unsigned n = 0;
    while (n < capacity && o->count)
    {
        out[n++] = o->samples[o->head];
        o->head = (o->head + 1) % Capacity;
        --o->count;
    }
    if (dropped)
    {
        *dropped = o->dropped;
        o->dropped = 0;
    }
    ReleaseSRWLockExclusive(&o->lock);
    return n;
}
extern "C" int doom_motion_observer_stop(DoomMotionObserver *o)
{
    if (!o)
        return 1;
    doom_motion_observer_end(o);
    bool ok = true;
    for (int i = 5; i >= 0; --i)
    {
        if (o->slots[i].installed &&
            !replace_slot(o->slots[i], o->slots[i].replacement, o->slots[i].original))
            ok = false;
        if (o->slots[i].protection_failed)
            ok = false;
    }
    if (ok)
    {
        DoomMotionObserver *expected = o;
        current.compare_exchange_strong(expected, nullptr);
    }
    return ok ? 1 : 0;
}
extern "C" void doom_motion_observer_destroy(DoomMotionObserver *o)
{
    if (!o)
        return;
    (void)doom_motion_observer_stop(o);
    if (!o->published)
        delete o;
}
#else
struct DoomMotionObserver
{
};
extern "C" DoomMotionObserver *doom_motion_observer_create(void *, void *, int)
{
    return nullptr;
}
extern "C" int doom_motion_observer_start(DoomMotionObserver *)
{
    return 0;
}
extern "C" int doom_motion_observer_begin(DoomMotionObserver *, void *, void *, uint32_t,
                                          const float *, uint32_t, int, int)
{
    return 0;
}
extern "C" void doom_motion_observer_note_input(DoomMotionObserver *, int, float)
{
}
extern "C" void doom_motion_observer_end(DoomMotionObserver *)
{
}
extern "C" unsigned doom_motion_observer_drain(DoomMotionObserver *, DoomMotionSample *, unsigned,
                                               unsigned *)
{
    return 0;
}
extern "C" int doom_motion_observer_stop(DoomMotionObserver *)
{
    return 1;
}
extern "C" void doom_motion_observer_destroy(DoomMotionObserver *)
{
}
#endif
