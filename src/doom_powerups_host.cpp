#include "doom_powerups_host.h"
#include "doom_core.h"
#include "doom_world_lights.h"
#include <windows.h>
#include <new>

struct DoomMuzzlePulse {
    uint32_t token;
    ULONGLONG expires;
    float offset[3];
};
struct DoomPowerupsHost {
    void *module, *level;
    uint32_t generation, light;
    DoomWorldLights *lights;
    DoomMuzzlePulse muzzle[4];
};

namespace {
bool owns(DoomPowerupsHost *host, void *level, uint32_t generation) {
    return host && level && host->level == level && host->generation == generation;
}
bool clear_pulse(DoomPowerupsHost *host, DoomMuzzlePulse &pulse) {
    if (pulse.token && !doom_world_lights_remove(host->lights, host->level, host->generation, pulse.token))
        return false;
    pulse = {};
    return true;
}
}

extern "C" DoomPowerupsHost *doom_powerups_host_create(void *module, void *level, uint32_t generation) {
    if (!module || !level || !generation) return nullptr;
    auto *host = new (std::nothrow) DoomPowerupsHost{};
    if (!host) return nullptr;
    host->module = module; host->level = level; host->generation = generation;
    return host;
}

extern "C" int doom_powerups_host_update(DoomPowerupsHost *host, void *level, uint32_t generation,
                                          int light_amp, const float camera[3]) {
    if (!host || !level || host->level != level || host->generation != generation) return 0;
    if (!light_amp) {
        if (host->light && !doom_world_lights_remove(host->lights, level, generation, host->light)) return 0;
        host->light = 0;
        return 1;
    }
    if (!camera) return 0;
    if (!host->lights) {
        host->lights = doom_world_lights_create(host->module, level, generation);
        if (!host->lights) return 0;
    }
    if (!host->light) {
        host->light = doom_world_lights_add(host->lights, level, generation, camera, DOOM_WORLD_LIGHT_AMP);
        return host->light != 0;
    }
    return doom_world_lights_move(host->lights, level, generation, host->light, camera);
}

extern "C" int doom_powerups_host_muzzle_pulse(DoomPowerupsHost *host, void *level,
                                                uint32_t generation, int weapon, const float camera[3],
                                                const float forward[3]) {
    if (!owns(host, level, generation)) return 0;
    int kind = DOOM_WORLD_LIGHT_MUZZLE_AMBER;
    ULONGLONG duration = 100;
    switch (weapon) {
    case DOOM_FIST: case DOOM_CHAINSAW: return 1;
    case DOOM_PISTOL: case DOOM_SHOTGUN: case DOOM_SUPER_SHOTGUN:
    case DOOM_CHAINGUN: case DOOM_ROCKET: break;
    case DOOM_PLASMA: kind = DOOM_WORLD_LIGHT_MUZZLE_PLASMA; break;
    case DOOM_BFG: kind = DOOM_WORLD_LIGHT_MUZZLE_BFG; duration = 200; break;
    default: return 0;
    }
    if (!camera) return 0;
    if (!host->lights) {
        host->lights = doom_world_lights_create(host->module, level, generation);
        if (!host->lights) return 0;
    }
    const ULONGLONG now = GetTickCount64();
    DoomMuzzlePulse *slot = nullptr;
    for (auto &pulse : host->muzzle) {
        if (pulse.token && now >= pulse.expires && !clear_pulse(host, pulse)) return 0;
        if (!pulse.token && !slot) slot = &pulse;
    }
    if (!slot) {
        slot = &host->muzzle[0];
        for (auto &pulse : host->muzzle) if (pulse.expires < slot->expires) slot = &pulse;
        if (!clear_pulse(host, *slot)) return 0;
    }
    float offset[3]{}, position[3]{};
    if (kind==DOOM_WORLD_LIGHT_MUZZLE_AMBER && forward) {
        for (unsigned i=0;i<3;++i) offset[i]=forward[i]*0.45f;
        offset[1]-=0.08f;
    }
    for (unsigned i=0;i<3;++i) position[i]=camera[i]+offset[i];
    const uint32_t token = doom_world_lights_add(host->lights, level, generation, position, kind);
    if (!token) return 0;
    *slot = {token, now + duration, {offset[0],offset[1],offset[2]}};
    return 1;
}

extern "C" int doom_powerups_host_muzzle_tick(DoomPowerupsHost *host, void *level,
                                               uint32_t generation, int enabled, const float camera[3]) {
    if (!owns(host, level, generation)) return 0;
    const ULONGLONG now = GetTickCount64();
    int ok = 1;
    for (auto &pulse : host->muzzle) {
        if (!pulse.token) continue;
        if (!enabled || now >= pulse.expires) {
            if (!clear_pulse(host, pulse)) ok = 0;
        } else {
            float position[3]{};
            if (camera) for (unsigned i=0;i<3;++i) position[i]=camera[i]+pulse.offset[i];
            if (!camera || !doom_world_lights_move(host->lights, level, generation, pulse.token, position)) ok=0;
        }
    }
    return ok;
}

extern "C" int doom_powerups_host_destroy(DoomPowerupsHost *host, void *level, uint32_t generation) {
    if (!host) return 1;
    if (!level || host->level != level || host->generation != generation) return 0;
    if (host->lights && !doom_world_lights_destroy(host->lights, level, generation)) return 0;
    delete host;
    return 1;
}
