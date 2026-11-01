#include "doom_player_impulse.h"
#include <windows.h>
#include <cmath>

namespace {
struct Vec3 { float x, y, z; };
using GetLevel = void *(__fastcall *)(void *);
using PhysicsBind = void *(__fastcall *)(void *);
using GetConfig = void *(__fastcall *)(void *);
using GetBody = void *(__fastcall *)(void *, unsigned);
using GetVelocity = const Vec3 *(__fastcall *)(void *);
using SetVelocity = void (__fastcall *)(void *, const Vec3 *);
}

extern "C" int doom_player_impulse(void *player, void *level, void *engine,
                                   float direction_x, float direction_z, float speed) {
    if (!player || !level || !engine || !std::isfinite(direction_x) ||
        !std::isfinite(direction_z) || !std::isfinite(speed) || speed <= 0.0f || speed > 0.8f)
        return 0;
    const float length = std::sqrt(direction_x * direction_x + direction_z * direction_z);
    if (!std::isfinite(length) || length < 0.0001f) return 0;
    auto module = static_cast<HMODULE>(engine);
    auto get_level = reinterpret_cast<GetLevel>(GetProcAddress(module, "?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
    auto physics_bind = reinterpret_cast<PhysicsBind>(GetProcAddress(module, "?PhysicsBind@IControlObject@@QEBAPEBVIPhysicsBind@@XZ"));
    auto get_velocity = reinterpret_cast<GetVelocity>(GetProcAddress(module, "?GetLinearVel@IPhBody@@QEBAAEBVvec3@@XZ"));
    auto set_velocity = reinterpret_cast<SetVelocity>(GetProcAddress(module, "?SetLinearVel@IPhBody@@QEAAXAEBVvec3@@@Z"));
    if (!get_level || !physics_bind || !get_velocity || !set_velocity) return 0;
    __try {
        auto bytes = static_cast<unsigned char *>(player);
        if (get_level(bytes + 40) != level) return 0;
        void *bind = physics_bind(bytes + 24);
        if (!bind) return 0;
        auto bind_vtable = *reinterpret_cast<void ***>(bind);
        if (!bind_vtable || !bind_vtable[20]) return 0;
        void *config = reinterpret_cast<GetConfig>(bind_vtable[20])(bind);
        if (!config) return 0;
        auto config_vtable = *reinterpret_cast<void ***>(config);
        if (!config_vtable || !config_vtable[34]) return 0;
        void *body = reinterpret_cast<GetBody>(config_vtable[34])(config, 0);
        if (!body) return 0;
        const Vec3 *current = get_velocity(body);
        if (!current || !std::isfinite(current->x) || !std::isfinite(current->y) || !std::isfinite(current->z)) return 0;
        Vec3 next = *current;
        next.x += direction_x / length * speed;
        next.z += direction_z / length * speed;
        if (!std::isfinite(next.x) || !std::isfinite(next.z)) return 0;
        set_velocity(body, &next);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
