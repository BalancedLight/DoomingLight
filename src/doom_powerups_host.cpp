#include "doom_powerups_host.h"
#include "doom_world_lights.h"
#include <new>

struct DoomPowerupsHost {
    void *module, *level;
    uint32_t generation, light;
    DoomWorldLights *lights;
};

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

extern "C" int doom_powerups_host_destroy(DoomPowerupsHost *host, void *level, uint32_t generation) {
    if (!host) return 1;
    if (!level || host->level != level || host->generation != generation) return 0;
    if (host->lights && !doom_world_lights_destroy(host->lights, level, generation)) return 0;
    delete host;
    return 1;
}
